"""Tests for the payment server. Stripe's API is replaced by fakes; webhook signatures are real
(signed with a test secret the same way Stripe signs them)."""
import hashlib
import hmac
import json
import os
import sys
import time

import pytest
from fastapi.testclient import TestClient

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..'))
import app as server  # noqa: E402

WHSEC = 'whsec_test_secret'


class FakeSessions:
    def __init__(self):
        self.created = []
        self.status = {}   # session id -> payment_status

    def create(self, **kw):
        sid = 'cs_test_%d' % (len(self.created) + 1)
        self.created.append(kw)
        self.status[sid] = 'unpaid'
        return {'id': sid, 'url': 'https://checkout.stripe.com/c/pay/' + sid}

    def retrieve(self, sid):
        return {'id': sid, 'payment_status': self.status.get(sid, 'unpaid'), 'status': 'open'}


@pytest.fixture
def client(tmp_path, monkeypatch):
    monkeypatch.setenv('STRIPE_SECRET_KEY', 'sk_test_x')
    monkeypatch.setenv('STRIPE_WEBHOOK_SECRET', WHSEC)
    monkeypatch.setenv('PUBLIC_URL', 'https://pay.example')
    monkeypatch.setenv('DB_PATH', str(tmp_path / 'p.db'))
    fake = FakeSessions()
    monkeypatch.setattr(server.stripe.checkout.Session, 'create', fake.create)
    monkeypatch.setattr(server.stripe.checkout.Session, 'retrieve', fake.retrieve)
    c = TestClient(server.app)
    c.fake = fake
    return c


def signed(payload):
    t = str(int(time.time()))
    sig = hmac.new(WHSEC.encode(), (t + '.' + payload).encode(), hashlib.sha256).hexdigest()
    return {'stripe-signature': 't=%s,v1=%s' % (t, sig), 'content-type': 'application/json'}


def completed(session_id, pid, paid=True):
    return json.dumps({'id': 'evt_1', 'object': 'event', 'type': 'checkout.session.completed',
                       'data': {'object': {'id': session_id, 'object': 'checkout.session',
                                           'payment_status': 'paid' if paid else 'unpaid',
                                           'client_reference_id': pid, 'metadata': {'purchase_id': pid}}}})


def test_packs_are_the_old_vbucks_prices(client):
    p = client.get('/packs').json()
    got = {x['kbucks']: x['cents'] for x in p['packs']}
    assert got == {1000: 799, 2800: 1999, 5000: 3199, 13500: 7999}
    assert p['currency'] == 'usd'


def test_checkout_makes_a_stripe_session_with_the_right_price(client):
    r = client.post('/checkout', json={'account': 'kingston', 'pack': 'kb2800'})
    assert r.status_code == 200
    body = r.json()
    assert body['url'].startswith('https://checkout.stripe.com/')
    assert body['kbucks'] == 2800 and len(body['token']) >= 32
    kw = client.fake.created[0]
    assert kw['mode'] == 'payment'
    assert kw['line_items'][0]['price_data']['unit_amount'] == 1999
    assert kw['metadata']['account'] == 'KINGSTON'
    assert kw['success_url'].startswith('https://pay.example/done')


def test_bad_requests(client):
    assert client.post('/checkout', json={'account': 'kingston', 'pack': 'free_money'}).status_code == 400
    assert client.post('/checkout', json={'account': 'bad name!', 'pack': 'kb1000'}).status_code == 400


def test_full_purchase_paid_by_webhook_and_claimed_once(client):
    b = client.post('/checkout', json={'account': 'KINGSTON', 'pack': 'kb1000'}).json()
    pid, tok = b['purchase'], b['token']
    assert client.get('/purchase/%s?token=%s' % (pid, tok)).json()['status'] == 'pending'
    # Not paid yet: can't claim.
    assert client.post('/purchase/%s/claim' % pid, json={'token': tok}).status_code == 402
    payload = completed('cs_test_1', pid)
    assert client.post('/webhook', content=payload, headers=signed(payload)).status_code == 200
    assert client.get('/purchase/%s?token=%s' % (pid, tok)).json()['status'] == 'paid'
    r = client.post('/purchase/%s/claim' % pid, json={'token': tok})
    assert r.status_code == 200 and r.json()['kbucks'] == 1000
    # A second claim is refused: K-Bucks are handed out once.
    assert client.post('/purchase/%s/claim' % pid, json={'token': tok}).status_code == 409


def test_forged_webhook_is_rejected(client):
    b = client.post('/checkout', json={'account': 'KINGSTON', 'pack': 'kb13500'}).json()
    payload = completed('cs_test_1', b['purchase'])
    bad = {'stripe-signature': 't=1,v1=deadbeef', 'content-type': 'application/json'}
    assert client.post('/webhook', content=payload, headers=bad).status_code == 400
    assert client.get('/purchase/%s?token=%s' % (b['purchase'], b['token'])).json()['status'] == 'pending'


def test_webhook_for_another_session_does_not_pay(client):
    b = client.post('/checkout', json={'account': 'KINGSTON', 'pack': 'kb5000'}).json()
    payload = completed('cs_someone_else', b['purchase'])
    client.post('/webhook', content=payload, headers=signed(payload))
    assert client.get('/purchase/%s?token=%s' % (b['purchase'], b['token'])).json()['status'] == 'pending'


def test_wrong_token_cannot_see_or_claim(client):
    b = client.post('/checkout', json={'account': 'KINGSTON', 'pack': 'kb1000'}).json()
    assert client.get('/purchase/%s?token=nope' % b['purchase']).status_code == 404
    assert client.post('/purchase/%s/claim' % b['purchase'], json={'token': 'nope'}).status_code == 404


def test_paid_found_without_webhook(client):
    # Webhook never arrives, but Stripe says the session is paid: polling picks it up.
    b = client.post('/checkout', json={'account': 'KINGSTON', 'pack': 'kb1000'}).json()
    client.fake.status['cs_test_1'] = 'paid'
    assert client.get('/purchase/%s?token=%s' % (b['purchase'], b['token'])).json()['status'] == 'paid'
    assert client.post('/purchase/%s/claim' % b['purchase'], json={'token': b['token']}).status_code == 200


def test_store_off_without_key(client, monkeypatch):
    monkeypatch.setenv('STRIPE_SECRET_KEY', '')
    assert client.post('/checkout', json={'account': 'KINGSTON', 'pack': 'kb1000'}).status_code == 503

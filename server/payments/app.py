"""Blockfall payment server: sells K-Bucks packs through Stripe Checkout.

How a purchase works:
  1. The game calls POST /checkout with the account name and a pack. The server makes a purchase
     record and a Stripe Checkout Session, and returns the Checkout page URL, the purchase id and a
     secret claim token (only the game that started the purchase knows it).
  2. The game opens the URL in the browser. The player pays on Stripe's page (card details never
     reach the game or this server).
  3. Stripe calls POST /webhook (signed). The server marks the purchase paid. If the webhook is
     late or not set up, GET /purchase/{id} also asks Stripe directly.
  4. The game polls GET /purchase/{id}; once it says "paid" the game calls POST /purchase/{id}/claim.
     A purchase can be claimed once; the game then adds the K-Bucks to the account.

Settings (environment variables):
  STRIPE_SECRET_KEY      sk_test_... or sk_live_...
  STRIPE_WEBHOOK_SECRET  whsec_... (from the webhook endpoint you add in the Stripe dashboard)
  PUBLIC_URL             where this server is reachable, e.g. https://blockfall-pay.onrender.com
  CURRENCY               default "usd"
  DB_PATH                default "payments.db" (SQLite)
"""
import os
import re
import secrets
import sqlite3
import time
from contextlib import contextmanager

import stripe
from fastapi import FastAPI, HTTPException, Request
from fastapi.responses import HTMLResponse
from pydantic import BaseModel

# K-Bucks packs, priced like Fortnite's V-Bucks before the October 2023 increase.
PACKS = {
    'kb1000': {'kbucks': 1000, 'cents': 799},
    'kb2800': {'kbucks': 2800, 'cents': 1999},
    'kb5000': {'kbucks': 5000, 'cents': 3199},
    'kb13500': {'kbucks': 13500, 'cents': 7999},
}
PENDING_TTL = 24 * 3600          # an unpaid purchase is dropped after a day
NAME_RE = re.compile(r'^[A-Z0-9_]{1,24}$')


def settings():
    return {
        'key': os.environ.get('STRIPE_SECRET_KEY', ''),
        'whsec': os.environ.get('STRIPE_WEBHOOK_SECRET', ''),
        'public': os.environ.get('PUBLIC_URL', 'http://localhost:8000').rstrip('/'),
        'currency': os.environ.get('CURRENCY', 'usd').lower(),
        'db': os.environ.get('DB_PATH', 'payments.db'),
    }


@contextmanager
def db():
    con = sqlite3.connect(settings()['db'])
    con.row_factory = sqlite3.Row
    con.execute('''CREATE TABLE IF NOT EXISTS purchases (
        id TEXT PRIMARY KEY, token TEXT NOT NULL, account TEXT NOT NULL, pack TEXT NOT NULL,
        kbucks INTEGER NOT NULL, cents INTEGER NOT NULL, currency TEXT NOT NULL,
        session TEXT, status TEXT NOT NULL, created REAL NOT NULL, paid REAL, claimed REAL)''')
    try:
        yield con
        con.commit()
    finally:
        con.close()


app = FastAPI(title='Blockfall payments')


class CheckoutIn(BaseModel):
    account: str
    pack: str


class ClaimIn(BaseModel):
    token: str


@app.get('/health')
def health():
    return {'ok': True}


@app.get('/packs')
def packs():
    cur = settings()['currency']
    return {'currency': cur, 'packs': [{'id': k, 'kbucks': v['kbucks'], 'cents': v['cents']} for k, v in PACKS.items()]}


@app.post('/checkout')
def checkout(body: CheckoutIn):
    cfg = settings()
    if not cfg['key']:
        raise HTTPException(503, 'store not configured (no Stripe key)')
    account = body.account.strip().upper()
    if not NAME_RE.match(account):
        raise HTTPException(400, 'bad account name')
    pack = PACKS.get(body.pack)
    if not pack:
        raise HTTPException(400, 'unknown pack')
    pid = secrets.token_hex(12)
    token = secrets.token_hex(24)
    stripe.api_key = cfg['key']
    session = stripe.checkout.Session.create(
        mode='payment',
        line_items=[{
            'quantity': 1,
            'price_data': {
                'currency': cfg['currency'],
                'unit_amount': pack['cents'],
                'product_data': {'name': '{:,} K-Bucks'.format(pack['kbucks']),
                                 'description': 'Blockfall in-game currency for ' + account},
            },
        }],
        client_reference_id=pid,
        metadata={'purchase_id': pid, 'account': account, 'pack': body.pack},
        success_url=cfg['public'] + '/done?purchase=' + pid,
        cancel_url=cfg['public'] + '/cancelled?purchase=' + pid,
    )
    session = plain(session)
    with db() as con:
        con.execute('INSERT INTO purchases VALUES (?,?,?,?,?,?,?,?,?,?,?,?)',
                    (pid, token, account, body.pack, pack['kbucks'], pack['cents'], cfg['currency'],
                     session['id'], 'pending', time.time(), None, None))
    return {'purchase': pid, 'token': token, 'url': session['url'], 'kbucks': pack['kbucks']}



def plain(o):
    """Stripe objects (stripe-python 8+) aren't dicts; turn them (and our test fakes) into dicts."""
    if hasattr(o, 'to_dict'):
        return o.to_dict()
    return dict(o)


def mark_paid(con, pid, session_id=None):
    row = con.execute('SELECT * FROM purchases WHERE id=?', (pid,)).fetchone()
    if not row or row['status'] != 'pending':
        return
    if session_id and row['session'] and session_id != row['session']:
        return
    con.execute("UPDATE purchases SET status='paid', paid=? WHERE id=? AND status='pending'", (time.time(), pid))


@app.post('/webhook')
async def webhook(request: Request):
    cfg = settings()
    payload = await request.body()
    try:
        event = stripe.Webhook.construct_event(payload, request.headers.get('stripe-signature', ''), cfg['whsec'])
    except Exception:
        raise HTTPException(400, 'bad signature')
    if event['type'] in ('checkout.session.completed', 'checkout.session.async_payment_succeeded'):
        s = plain(event['data']['object'])
        if s.get('payment_status') == 'paid':
            pid = (s.get('metadata') or {}).get('purchase_id') or s.get('client_reference_id')
            if pid:
                with db() as con:
                    mark_paid(con, pid, s.get('id'))
    return {'received': True}


def load(con, pid, token):
    row = con.execute('SELECT * FROM purchases WHERE id=?', (pid,)).fetchone()
    if not row or not secrets.compare_digest(row['token'], token or ''):
        raise HTTPException(404, 'no such purchase')
    return row


@app.get('/purchase/{pid}')
def purchase(pid: str, token: str = ''):
    cfg = settings()
    with db() as con:
        row = load(con, pid, token)
        status = row['status']
        if status == 'pending':
            # The webhook may be late or missing: ask Stripe directly.
            if cfg['key'] and row['session']:
                try:
                    stripe.api_key = cfg['key']
                    s = plain(stripe.checkout.Session.retrieve(row['session']))
                    if s.get('payment_status') == 'paid':
                        mark_paid(con, pid, s.get('id'))
                        status = 'paid'
                    elif s.get('status') == 'expired':
                        con.execute("UPDATE purchases SET status='expired' WHERE id=?", (pid,))
                        status = 'expired'
                except Exception:
                    pass
            if status == 'pending' and time.time() - row['created'] > PENDING_TTL:
                con.execute("UPDATE purchases SET status='expired' WHERE id=?", (pid,))
                status = 'expired'
        return {'purchase': pid, 'status': status, 'kbucks': row['kbucks'], 'account': row['account']}


@app.post('/purchase/{pid}/claim')
def claim(pid: str, body: ClaimIn):
    with db() as con:
        row = load(con, pid, body.token)
        if row['status'] == 'claimed':
            raise HTTPException(409, 'already claimed')
        if row['status'] != 'paid':
            raise HTTPException(402, 'not paid')
        cur = con.execute("UPDATE purchases SET status='claimed', claimed=? WHERE id=? AND status='paid'", (time.time(), pid))
        if cur.rowcount != 1:
            raise HTTPException(409, 'already claimed')
        return {'purchase': pid, 'kbucks': row['kbucks'], 'account': row['account']}


PAGE = '''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Blockfall</title><style>body{{font-family:system-ui,sans-serif;background:#141a2c;color:#eef;display:flex;
align-items:center;justify-content:center;min-height:100vh;margin:0}}div{{max-width:28rem;padding:2rem;text-align:center}}
h1{{color:{color}}}</style></head><body><div><h1>{title}</h1><p>{text}</p></div></body></html>'''


@app.get('/done', response_class=HTMLResponse)
def done():
    return PAGE.format(color='#ffc440', title='Thanks!', text='Your K-Bucks are on their way. Go back to Blockfall; they appear in a few seconds.')


@app.get('/cancelled', response_class=HTMLResponse)
def cancelled():
    return PAGE.format(color='#ff7a6a', title='No charge', text='The purchase was cancelled and nothing was charged. You can close this tab.')

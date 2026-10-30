# Blockfall payment server (K-Bucks through Stripe)

A small web server that sells K-Bucks packs with **Stripe Checkout**. The game never sees card
details: it asks this server for a Stripe payment page, opens it in the player's browser, and
adds the K-Bucks once Stripe confirms the payment. Each purchase can be claimed only once.

| Pack | Price (USD) |
|---|---|
| 1,000 K-Bucks | $7.99 |
| 2,800 K-Bucks | $19.99 |
| 5,000 K-Bucks | $31.99 |
| 13,500 K-Bucks | $79.99 |

(Fortnite's V-Bucks prices before the October 2023 increase. Change them in `PACKS` in `app.py`
**and** `KBUCKS_PACKS` in `src/game/store.h`.)

## Set it up (about 20 minutes)

### 1. Stripe
1. Make a Stripe account at <https://dashboard.stripe.com> (free). Stay in **Test mode** (toggle
   at the top) until everything works.
2. **Developers → API keys**: copy the **Secret key** (`sk_test_...`).

### 2. Put the server online
Any host that runs Python works. With **Render** (<https://render.com>):
1. New → **Web Service** → connect your `blockfall` GitHub repo.
2. **Root directory:** `server/payments`
3. **Build command:** `pip install -r requirements.txt`
4. **Start command:** `uvicorn app:app --host 0.0.0.0 --port $PORT`
5. **Add a disk** (Advanced → Disk, mount path `/data`, 1 GB) so purchases survive restarts.
   That needs a paid instance (Starter, about $7/month). The free tier wipes its files, which
   would lose purchases that haven't been claimed yet.
6. **Environment variables:**
   - `STRIPE_SECRET_KEY` = your `sk_test_...` key
   - `PUBLIC_URL` = the address Render gives you, e.g. `https://blockfall-pay.onrender.com`
   - `DB_PATH` = `/data/payments.db`
   - `STRIPE_WEBHOOK_SECRET` = (step 3)
7. Deploy. Opening `https://<your address>/packs` should show the four packs.

(Railway or Fly.io work the same way: run `uvicorn app:app --host 0.0.0.0 --port $PORT` from
`server/payments`, with a volume for `DB_PATH`.)

### 3. Stripe webhook
1. Stripe dashboard → **Developers → Webhooks → Add endpoint**.
2. URL: `https://<your address>/webhook`
3. Events: `checkout.session.completed` and `checkout.session.async_payment_succeeded`.
4. Copy the **Signing secret** (`whsec_...`) into `STRIPE_WEBHOOK_SECRET` on your host and redeploy.

(Without the webhook it still works, because the game's checks ask Stripe directly. The webhook
makes it instant and is the recommended setup.)

### 4. Point the game at it
Either:
- **For everyone:** put your address in `STORE_DEFAULT_URL` in `src/game/store.h`, rebuild, and
  commit the new `bin/Blockfall.exe`, or
- **Just your PC (testing):** add a line `store=https://<your address>` to
  `%APPDATA%\Blockfall\settings.ini`.

In the game: **Item Shop → GET K-BUCKS → BUY**. Pay with Stripe's test card
`4242 4242 4242 4242`, any future date, any CVC. The K-Bucks appear a few seconds later.

### 5. Go live
When test purchases work: finish Stripe's account activation (business details, bank account),
switch the dashboard to **Live mode**, add a live webhook (step 3 again), and put the live
`sk_live_...` and `whsec_...` values on your host.

## Good to know
- **Cheating:** K-Bucks balances still live in each player's account file on their own PC, so
  someone who edits that file can give themselves K-Bucks. Real protection needs accounts kept
  on a server. This server already records every purchase and who it was for, so it can grow
  into that.
- **Refunds:** refund in the Stripe dashboard. The game doesn't take K-Bucks back automatically.
- **Taxes and rules:** selling digital currency can come with sales tax and consumer rules where
  your players live. Stripe Tax can handle the tax part, and you'll want a short terms page (no
  cash value, non-refundable once used, and so on). Check what applies to you.
- **Tests:** `pip install -r requirements.txt pytest httpx`, then `python -m pytest tests`.
  Stripe is replaced by fakes; webhook signatures are checked for real.

## API (what the game calls)
| Call | What it does |
|---|---|
| `GET /packs` | The packs and prices |
| `POST /checkout {account, pack}` | Makes a purchase and a Stripe Checkout page. Returns `purchase`, `token` (secret), `url` |
| `POST /webhook` | Stripe says a payment went through (signature checked) |
| `GET /purchase/{id}?token=` | `pending` / `paid` / `claimed` / `expired` |
| `POST /purchase/{id}/claim {token}` | Once paid: returns the K-Bucks, once only (`409` after that) |

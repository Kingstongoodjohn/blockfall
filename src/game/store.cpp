// Blockfall - the K-Bucks store (see store.h)
#include "store.h"
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <cstdlib>

namespace bf {

std::string formatPrice(int cents) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "$%d.%02d", cents / 100, cents % 100);
    return buf;
}

std::string formatKBucks(int n) {
    std::string s = std::to_string(n);
    for (int i = (int)s.size() - 3; i > 0; i -= 3) s.insert((size_t)i, ",");
    return s;
}

namespace {
// Finds `"key"` followed by `:` and returns the position just after the colon (spaces skipped).
size_t valueAt(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t at = 0;
    while ((at = json.find(needle, at)) != std::string::npos) {
        size_t p = at + needle.size();
        while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) ++p;
        if (p < json.size() && json[p] == ':') {
            ++p;
            while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\n' || json[p] == '\r')) ++p;
            return p;
        }
        at = p;
    }
    return std::string::npos;
}

std::string escape(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '"' || c == '\\') o += '\\';
        if ((unsigned char)c >= 0x20) o += c;
    }
    return o;
}
}  // namespace

std::string jsonString(const std::string& json, const std::string& key) {
    size_t p = valueAt(json, key);
    if (p == std::string::npos || p >= json.size() || json[p] != '"') return "";
    std::string out;
    for (size_t i = p + 1; i < json.size(); ++i) {
        char c = json[i];
        if (c == '\\' && i + 1 < json.size()) {
            char n = json[++i];
            out += n == 'n' ? '\n' : (n == 't' ? '\t' : n);   // the server only sends plain text and URLs
        } else if (c == '"') {
            return out;
        } else {
            out += c;
        }
    }
    return "";
}

long long jsonInt(const std::string& json, const std::string& key, long long fallback) {
    size_t p = valueAt(json, key);
    if (p == std::string::npos || p >= json.size()) return fallback;
    char* end = nullptr;
    long long v = std::strtoll(json.c_str() + p, &end, 10);
    return end == json.c_str() + p ? fallback : v;
}

std::string PendingPurchase::save() const { return id + ":" + token + ":" + std::to_string(kbucks); }

bool PendingPurchase::load(const std::string& s, PendingPurchase& out) {
    size_t a = s.find(':'), b = a == std::string::npos ? a : s.find(':', a + 1);
    if (b == std::string::npos) return false;
    out.id = s.substr(0, a);
    out.token = s.substr(a + 1, b - a - 1);
    out.kbucks = std::atoi(s.c_str() + b + 1);
    auto ok = [](const std::string& x) {
        return !x.empty() && x.size() <= 128 && std::all_of(x.begin(), x.end(), [](char c) { return std::isalnum((unsigned char)c) != 0; });
    };
    return ok(out.id) && ok(out.token) && out.kbucks > 0;
}

StoreClient::~StoreClient() {
    {
        std::lock_guard<std::mutex> lk(mu_);
        stop_ = true;
    }
    cv_.notify_all();
    if (thread_.joinable()) thread_.join();
}

void StoreClient::setServer(const std::string& url) {
    server_ = url;
    while (!server_.empty() && (server_.back() == '/' || server_.back() == ' ')) server_.pop_back();
}

void StoreClient::worker() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait(lk, [&] { return stop_ || !todo_.empty(); });
            if (stop_) return;
            job = std::move(todo_.front());
            todo_.pop_front();
        }
        job.res = transport_ ? transport_(job.req) : HttpResponse{0, "", "no network"};
        std::lock_guard<std::mutex> lk(mu_);
        done_.push_back(std::move(job));
    }
}

void StoreClient::submit(Job job) {
    ++inFlight_;
    if (!async_) {
        job.res = transport_ ? transport_(job.req) : HttpResponse{0, "", "no network"};
        std::lock_guard<std::mutex> lk(mu_);
        done_.push_back(std::move(job));
        return;
    }
    if (!thread_.joinable()) thread_ = std::thread([this] { worker(); });
    {
        std::lock_guard<std::mutex> lk(mu_);
        todo_.push_back(std::move(job));
    }
    cv_.notify_one();
}

bool StoreClient::buy(const std::string& account, int pack) {
    if (!configured()) {
        state_ = State::Failed;
        status_ = "THE STORE ISN'T SET UP YET";
        return false;
    }
    if (busy() || pack < 0 || pack >= KBUCKS_PACK_COUNT || account.empty()) return false;
    account_ = account;
    activePack_ = pack;
    active_.clear();
    waited_ = 0;
    state_ = State::Creating;
    status_ = "OPENING STRIPE CHECKOUT...";
    Job j;
    j.kind = 0;
    j.req = {"POST", server_ + "/checkout", "{\"account\": \"" + escape(account) + "\", \"pack\": \"" + KBUCKS_PACKS[pack].id + "\"}"};
    submit(std::move(j));
    return true;
}

void StoreClient::resume(const std::vector<PendingPurchase>& pending) {
    for (const PendingPurchase& p : pending) resumeList_.push_back(p);
    pollTimer_ = 0;   // check them on the next update
}

void StoreClient::stopWaiting() {
    if (state_ == State::WaitingForPayment) {
        state_ = State::Idle;
        status_ = "WE'LL KEEP CHECKING IN THE BACKGROUND - YOUR K-BUCKS ARRIVE ONCE IT'S PAID";
        active_.clear();
    }
}

PendingPurchase* StoreClient::find(std::vector<PendingPurchase>& pending, const std::string& id) {
    for (PendingPurchase& p : pending)
        if (p.id == id) return &p;
    return nullptr;
}

void StoreClient::update(float dt, int& credited, std::vector<PendingPurchase>& pending) {
    credited = 0;
    // Purchases remembered from an earlier run join the pending list.
    for (const PendingPurchase& p : resumeList_)
        if (!find(pending, p.id)) pending.push_back(p);
    resumeList_.clear();

    std::deque<Job> finished;
    {
        std::lock_guard<std::mutex> lk(mu_);
        finished.swap(done_);
    }
    for (Job& j : finished) {
        --inFlight_;
        handle(j, credited, pending);
    }

    if (state_ == State::WaitingForPayment) {
        waited_ += dt;
        if (waited_ > STORE_WAIT_SECONDS) stopWaiting();
    }
    // Poll every pending purchase now and then (faster for the one being paid right now).
    pollTimer_ -= dt;
    if (pollTimer_ <= 0 && configured()) {
        pollTimer_ = state_ == State::WaitingForPayment ? STORE_POLL_SECONDS : STORE_POLL_SECONDS * 5;
        for (const PendingPurchase& p : pending) {
            if (std::find(polling_.begin(), polling_.end(), p.id) != polling_.end()) continue;
            polling_.push_back(p.id);
            Job j;
            j.kind = 1;
            j.purchase = p.id;
            j.req = {"GET", server_ + "/purchase/" + p.id + "?token=" + p.token, ""};
            submit(std::move(j));
        }
    }
}

void StoreClient::handle(Job& j, int& credited, std::vector<PendingPurchase>& pending) {
    const HttpResponse& r = j.res;
    if (j.kind == 0) {   // checkout created
        std::string id = jsonString(r.body, "purchase"), token = jsonString(r.body, "token"), url = jsonString(r.body, "url");
        int kb = (int)jsonInt(r.body, "kbucks", 0);
        PendingPurchase p;
        p.id = id;
        p.token = token;
        p.kbucks = kb;
        if (r.status != 200 || url.rfind("https://", 0) != 0 || !PendingPurchase::load(p.save(), p)) {
            state_ = State::Failed;
            status_ = r.status == 0 ? "COULDN'T REACH THE STORE - CHECK YOUR INTERNET" : "THE STORE SAID NO (" + std::to_string(r.status) + ") - TRY AGAIN LATER";
            return;
        }
        pending.push_back(p);
        active_ = id;
        state_ = State::WaitingForPayment;
        waited_ = 0;
        pollTimer_ = STORE_POLL_SECONDS;
        status_ = "FINISH PAYING IN YOUR BROWSER - WAITING FOR STRIPE...";
        if (openUrl) openUrl(url);
        return;
    }
    if (j.kind == 1) {   // poll
        polling_.erase(std::remove(polling_.begin(), polling_.end(), j.purchase), polling_.end());
        PendingPurchase* p = find(pending, j.purchase);
        if (!p) return;
        std::string st = jsonString(r.body, "status");
        if (r.status == 404 || st == "expired") {
            // Never paid (or not ours): forget it.
            pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const PendingPurchase& q) { return q.id == j.purchase; }), pending.end());
            if (j.purchase == active_) {
                state_ = State::Failed;
                status_ = "THE CHECKOUT EXPIRED - NOTHING WAS CHARGED";
                active_.clear();
            }
            return;
        }
        if (st == "paid") {
            Job c;
            c.kind = 2;
            c.purchase = p->id;
            c.req = {"POST", server_ + "/purchase/" + p->id + "/claim", "{\"token\": \"" + p->token + "\"}"};
            if (j.purchase == active_) {
                state_ = State::Claiming;
                status_ = "PAID! ADDING YOUR K-BUCKS...";
            }
            polling_.push_back(p->id);   // no more polls until the claim answers
            submit(std::move(c));
        }
        return;
    }
    // Claim answered.
    polling_.erase(std::remove(polling_.begin(), polling_.end(), j.purchase), polling_.end());
    PendingPurchase* p = find(pending, j.purchase);
    if (!p) return;
    int kb = 0;
    bool forget = false;
    if (r.status == 200) {
        kb = (int)jsonInt(r.body, "kbucks", 0);
        if (kb <= 0) kb = p->kbucks;
        credited += kb;
        forget = true;
    } else if (r.status == 409) {
        forget = true;   // already claimed earlier (e.g. by this account before a crash): nothing to add
    }
    if (forget) {
        pending.erase(std::remove_if(pending.begin(), pending.end(), [&](const PendingPurchase& q) { return q.id == j.purchase; }), pending.end());
        if (j.purchase == active_ || state_ == State::Idle || state_ == State::Done || state_ == State::Failed) {
            state_ = State::Done;
            status_ = kb > 0 ? "+" + formatKBucks(kb) + " K-BUCKS ADDED. THANKS!" : "THAT PURCHASE WAS ALREADY ADDED";
            active_.clear();
        }
    } else if (j.purchase == active_) {
        state_ = State::WaitingForPayment;   // try the claim again on the next poll
    }
}

}  // namespace bf

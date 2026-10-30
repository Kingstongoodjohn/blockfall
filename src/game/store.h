// Blockfall - the K-Bucks store: buying K-Bucks with real money through the payment server
// (server/payments) and Stripe Checkout.
//
// The game never sees card details. It asks the server for a Stripe Checkout page, opens it in
// the browser, then checks the purchase every few seconds. Once Stripe says it's paid, the game
// claims it (the server allows one claim per purchase) and adds the K-Bucks to the account.
// Purchases still waiting are kept in the account file, so closing the game mid-purchase loses
// nothing: they are picked up again on the next start.
//
// Network calls go through an HttpTransport the platform provides (WinHTTP on Windows) and run
// on a worker thread, so the menus never freeze. Tests plug in a fake transport.
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace bf {

struct KBucksPack {
    const char* id;   // the server's pack id
    int kbucks;
    int cents;        // US dollars x 100
};
// Fortnite's V-Bucks prices from before the October 2023 increase.
constexpr KBucksPack KBUCKS_PACKS[] = {
    {"kb1000", 1000, 799},
    {"kb2800", 2800, 1999},
    {"kb5000", 5000, 3199},
    {"kb13500", 13500, 7999},
};
constexpr int KBUCKS_PACK_COUNT = (int)(sizeof(KBUCKS_PACKS) / sizeof(KBUCKS_PACKS[0]));
// Where the payment server lives once it's deployed (see server/payments/README.md). Empty =
// the store isn't set up yet; "store=" in settings.ini overrides it.
constexpr const char* STORE_DEFAULT_URL = "";
constexpr float STORE_POLL_SECONDS = 3.0f;        // how often to ask whether a purchase is paid
constexpr float STORE_WAIT_SECONDS = 30.0f * 60;  // stop watching actively after half an hour

std::string formatPrice(int cents);    // 799 -> "$7.99"
std::string formatKBucks(int n);       // 13500 -> "13,500"

struct HttpRequest {
    std::string method;   // "GET" or "POST"
    std::string url;
    std::string body;     // JSON for POST
};
struct HttpResponse {
    int status = 0;       // 0 = couldn't reach the server
    std::string body;
    std::string error;
};
using HttpTransport = std::function<HttpResponse(const HttpRequest&)>;

// Tiny JSON readers for the server's flat replies ({"key": "value", "n": 12}).
std::string jsonString(const std::string& json, const std::string& key);
long long jsonInt(const std::string& json, const std::string& key, long long fallback = -1);

// A purchase the game is waiting on: "id:token:kbucks" in the account file.
struct PendingPurchase {
    std::string id, token;
    int kbucks = 0;
    std::string save() const;
    static bool load(const std::string& s, PendingPurchase& out);
};

class StoreClient {
public:
    enum class State { Idle, Creating, WaitingForPayment, Claiming, Done, Failed };

    StoreClient() = default;
    ~StoreClient();
    StoreClient(const StoreClient&) = delete;
    StoreClient& operator=(const StoreClient&) = delete;

    void setServer(const std::string& url);
    bool configured() const { return !server_.empty(); }
    void setTransport(HttpTransport t) { transport_ = std::move(t); }
    // true: requests run on a worker thread (the game); false: they run inside the call (tests).
    void setAsync(bool on) { async_ = on; }

    // Starts buying a pack for `account`. The checkout URL comes back through openUrl.
    bool buy(const std::string& account, int pack);
    // Keep watching purchases saved from an earlier run.
    void resume(const std::vector<PendingPurchase>& pending);
    // Stop watching the current purchase actively (it stays pending and is checked again later).
    void stopWaiting();

    // Call every frame. Fills `credited` with K-Bucks to add (each amount once) and keeps
    // `pending` (what the account must remember) up to date.
    void update(float dt, int& credited, std::vector<PendingPurchase>& pending);

    State state() const { return state_; }
    const std::string& status() const { return status_; }   // a line for the screen
    bool busy() const { return state_ == State::Creating || state_ == State::WaitingForPayment || state_ == State::Claiming; }

    std::function<void(const std::string&)> openUrl;   // set by the platform: opens the browser

private:
    struct Job {
        int kind;          // 0 checkout, 1 poll, 2 claim
        std::string purchase;
        HttpRequest req;
        HttpResponse res;
    };
    void submit(Job job);
    void handle(Job& job, int& credited, std::vector<PendingPurchase>& pending);
    void worker();
    PendingPurchase* find(std::vector<PendingPurchase>& pending, const std::string& id);

    std::string server_;
    HttpTransport transport_;
    bool async_ = true;
    State state_ = State::Idle;
    std::string status_;
    std::string active_;          // purchase being watched closely (this session's buy)
    int activePack_ = -1;
    std::string account_;
    float pollTimer_ = 0, waited_ = 0;
    int inFlight_ = 0;
    std::vector<PendingPurchase> resumeList_;
    std::vector<std::string> polling_;   // ids with a poll on the way

    std::mutex mu_;
    std::condition_variable cv_;
    std::deque<Job> todo_, done_;
    std::thread thread_;
    bool stop_ = false;
};

}  // namespace bf

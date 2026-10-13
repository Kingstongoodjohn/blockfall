// Blockfall - local player accounts
//
// The first time the game runs you create an account (a name and a password). Everything you
// own - K-Bucks, skins, the equipped skin, your stats - is saved in that account's file, so it
// is still there when you sign in again (even from a fresh settings file). Accounts are stored
// on this PC in <settings folder>\accounts\<name>.ini; there is no online server.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace bf {

struct Account {
    std::string name;
    uint64_t passHash = 0;
    int kbucks = 0;
    uint64_t ownedSkins = 1;
    int equippedSkin = 0;
    int matches = 0, wins = 0, kills = 0;

    std::string serialize() const;
    void parse(const std::string& text);
};

// Salted hash of a password (the password itself is never written to disk).
uint64_t hashPassword(const std::string& name, const std::string& password);
// Upper-cases and trims a name the way player names are cleaned; empty if invalid.
std::string cleanAccountName(const std::string& raw);

class AccountStore {
public:
    // dir: folder that holds the account files (created when needed). Empty = in-memory only (tests).
    void setDirectory(const std::string& dir) { dir_ = dir; }
    bool exists(const std::string& name) const;
    // Creates an account. Fails if the name is taken or invalid, or the password is too short.
    bool create(const std::string& name, const std::string& password, Account& out, std::string& error);
    // Signs in. Fails if the account doesn't exist or the password is wrong.
    bool signIn(const std::string& name, const std::string& password, Account& out, std::string& error);
    // Reloads a remembered account without the password (the game checked it when it signed in).
    bool signInRaw(const std::string& name, Account& out) const { return read(name, out); }
    bool save(const Account& a);
    std::vector<std::string> names() const;

private:
    std::string pathFor(const std::string& name) const;
    bool read(const std::string& name, Account& out) const;
    std::string dir_;
    std::vector<Account> memory_;  // used when dir_ is empty
};

constexpr int MIN_PASSWORD_LEN = 4;

}  // namespace bf

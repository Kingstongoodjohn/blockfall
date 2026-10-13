// Blockfall - local player accounts
#include "account.h"
#include "settings.h"
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace bf {

std::string cleanAccountName(const std::string& raw) { return cleanName(raw); }

uint64_t hashPassword(const std::string& name, const std::string& password) {
    // FNV-1a over "name:password" with a fixed salt, stretched a little.
    std::string s = "blockfall:" + name + ":" + password;
    uint64_t h = 1469598103934665603ull;
    for (int round = 0; round < 64; ++round) {
        for (unsigned char c : s) {
            h ^= c;
            h *= 1099511628211ull;
        }
        h ^= h >> 29;
    }
    return h;
}

std::string Account::serialize() const {
    std::ostringstream o;
    o << "# Blockfall account\n";
    o << "name=" << name << "\n";
    o << "pass=" << passHash << "\n";
    o << "kbucks=" << kbucks << "\n";
    o << "skins=" << ownedSkins << "\n";
    o << "skin=" << equippedSkin << "\n";
    o << "matches=" << matches << "\n";
    o << "wins=" << wins << "\n";
    o << "kills=" << kills << "\n";
    return o.str();
}

void Account::parse(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "name") name = cleanAccountName(v);
        else if (k == "pass") passHash = std::strtoull(v.c_str(), nullptr, 10);
        else if (k == "kbucks") kbucks = std::max(0, std::atoi(v.c_str()));
        else if (k == "skins") ownedSkins = (uint64_t)std::strtoull(v.c_str(), nullptr, 10) | 1ull;
        else if (k == "skin") equippedSkin = std::max(0, std::atoi(v.c_str()));
        else if (k == "matches") matches = std::atoi(v.c_str());
        else if (k == "wins") wins = std::atoi(v.c_str());
        else if (k == "kills") kills = std::atoi(v.c_str());
    }
}

std::string AccountStore::pathFor(const std::string& name) const {
    std::string file = name;
    for (char& c : file)
        if (c == ' ') c = '_';
    return dir_ + "/" + file + ".ini";
}

bool AccountStore::read(const std::string& name, Account& out) const {
    if (dir_.empty()) {
        for (const Account& a : memory_)
            if (a.name == name) { out = a; return true; }
        return false;
    }
    std::ifstream f(pathFor(name), std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    out = Account();
    out.parse(ss.str());
    return out.name == name;
}

bool AccountStore::exists(const std::string& name) const {
    Account a;
    return read(name, a);
}

bool AccountStore::create(const std::string& rawName, const std::string& password, Account& out, std::string& error) {
    std::string name = cleanAccountName(rawName);
    if (name.empty()) { error = "ENTER A NAME (LETTERS AND NUMBERS)"; return false; }
    if ((int)password.size() < MIN_PASSWORD_LEN) { error = "PASSWORD NEEDS AT LEAST 4 CHARACTERS"; return false; }
    if (exists(name)) { error = "THAT NAME IS TAKEN - SIGN IN INSTEAD"; return false; }
    out = Account();
    out.name = name;
    out.passHash = hashPassword(name, password);
    if (!save(out)) { error = "COULD NOT SAVE THE ACCOUNT FILE"; return false; }
    return true;
}

bool AccountStore::signIn(const std::string& rawName, const std::string& password, Account& out, std::string& error) {
    std::string name = cleanAccountName(rawName);
    if (name.empty()) { error = "ENTER YOUR NAME"; return false; }
    Account a;
    if (!read(name, a)) { error = "NO ACCOUNT WITH THAT NAME - CREATE ONE"; return false; }
    if (a.passHash != hashPassword(name, password)) { error = "WRONG PASSWORD"; return false; }
    out = a;
    return true;
}

bool AccountStore::save(const Account& a) {
    if (a.name.empty()) return false;
    if (dir_.empty()) {
        for (Account& m : memory_)
            if (m.name == a.name) { m = a; return true; }
        memory_.push_back(a);
        return true;
    }
    std::error_code ec;
    std::filesystem::create_directories(dir_, ec);
    std::ofstream f(pathFor(a.name), std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << a.serialize();
    return (bool)f;
}

std::vector<std::string> AccountStore::names() const {
    std::vector<std::string> out;
    if (dir_.empty()) {
        for (const Account& a : memory_) out.push_back(a.name);
        return out;
    }
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(dir_, ec)) {
        std::string n = e.path().filename().string();
        if (n.size() > 4 && n.substr(n.size() - 4) == ".ini") {
            Account a;
            std::ifstream f(e.path(), std::ios::binary);
            std::stringstream ss;
            ss << f.rdbuf();
            a.parse(ss.str());
            if (!a.name.empty()) out.push_back(a.name);
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace bf

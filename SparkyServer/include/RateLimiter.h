#pragma once
// ---------------------------------------------------------------------------
// RateLimiter — sliding-window per-IP connection throttle.
//
// Keeps a deque of hit timestamps per IP.  On each Allow() call the window
// is swept and stale entries dropped before counting.  Thread-safe.
//
// Recommended configuration:
//   MAX_HITS = 10  per 60-second window — blocks brute-force auth loops
//   BAN_HITS = 30  per 60-second window — hard-blocks until next restart
//     (use with iptables/ufw for a persistent block)
// ---------------------------------------------------------------------------
#include <cstdint>
#include <string>
#include <deque>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <iostream>
#include <format>

class RateLimiter
{
public:
    // maxHits  — connections allowed per windowSec before throttling
    // banHits  — connections that trigger a hard-ban entry (logged for iptables)
    // windowSec — rolling window in seconds
    explicit RateLimiter(int maxHits  = 10,
                         int banHits  = 30,
                         int windowSec = 60)
        : m_maxHits(maxHits), m_banHits(banHits),
          m_windowMs((int64_t)windowSec * 1000)
    {}

    // Returns true if the IP should be allowed to proceed.
    // Logs and returns false if over the rate limit.
    bool Allow(const std::string& ip)
    {
        const int64_t now = NowMs();
        std::lock_guard lk(m_mu);

        if (m_hardBan.count(ip))
        {
            // Hard-banned IP — always reject
            return false;
        }

        auto& q = m_buckets[ip];
        Sweep(q, now);

        q.push_back(now);
        const int hits = (int)q.size();

        if (hits >= m_banHits)
        {
            m_hardBan.insert(ip);
            std::cout << std::format(
                "[RateLimit] HARD-BAN {} ({} hits in window) — add to firewall:\n"
                "  iptables -A INPUT -s {} -j DROP\n", ip, hits, ip);
            return false;
        }

        if (hits > m_maxHits)
        {
            std::cout << std::format(
                "[RateLimit] Throttle {} ({}/{} hits)\n", ip, hits, m_maxHits);
            return false;
        }

        return true;
    }

    // Prune empty buckets to avoid unbounded memory growth.
    // Call once per maintenance cycle (e.g. every 5 minutes).
    void Prune()
    {
        const int64_t now = NowMs();
        std::lock_guard lk(m_mu);
        for (auto it = m_buckets.begin(); it != m_buckets.end(); )
        {
            Sweep(it->second, now);
            it = it->second.empty() ? m_buckets.erase(it) : std::next(it);
        }
    }

    // Manually un-ban an IP (e.g. false positive).
    void Unban(const std::string& ip)
    {
        std::lock_guard lk(m_mu);
        m_hardBan.erase(ip);
        m_buckets.erase(ip);
    }

    // Dump currently hard-banned IPs (for scripting into iptables on startup).
    std::vector<std::string> HardBanned() const
    {
        std::lock_guard lk(m_mu);
        return std::vector<std::string>(m_hardBan.begin(), m_hardBan.end());
    }

private:
    static int64_t NowMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::steady_clock::now().time_since_epoch())
                   .count();
    }

    void Sweep(std::deque<int64_t>& q, int64_t now) const
    {
        while (!q.empty() && now - q.front() > m_windowMs)
            q.pop_front();
    }

    int     m_maxHits;
    int     m_banHits;
    int64_t m_windowMs;

    mutable std::mutex                              m_mu;
    std::unordered_map<std::string, std::deque<int64_t>> m_buckets;
    std::unordered_set<std::string>                     m_hardBan;
};

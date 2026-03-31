#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ESPmDNS.h>
#include "greenhouse_config.h"
#include "Log.h"

// Nexa smart plug local HTTP API (port 3000).
// Plugs are discovered via mDNS (_systemnexa2._tcp) and identified by hostname.
// State control: GET /state?v=0 (off), GET /state?v=1 (on), GET /state?v=-1 (toggle)
// See PROTOCOL.md and discovery.py in nexa-wifi for full protocol reference.

#define NEXA_MDNS_SERVICE "systemnexa2"
#define NEXA_MDNS_PROTO   "tcp"

struct NexaDiscoveredPlug {
    char      hostname[NEXA_HOST_LEN];
    IPAddress ip;
};

class NexaController {
public:
    bool plug_on[MAX_NEXA_PLUGS]        = {};   // Current commanded state
    bool plug_reachable[MAX_NEXA_PLUGS] = {};   // Last HTTP call succeeded

    void init()
    {
        // mDNS is already started by main.cpp (MDNS.begin(APP_NAME)).
        // We only use it for queryService() — no need to re-init here.
        Log.info("[NEXA] Nexa controller ready (mDNS discovery available)");
    }

    // Discover Nexa plugs on the network via mDNS service browse.
    // Blocking (~3 s). Returns number of found plugs (up to max_results).
    static int discover(NexaDiscoveredPlug* results, int max_results)
    {
        int n = MDNS.queryService(NEXA_MDNS_SERVICE, NEXA_MDNS_PROTO);
        int count = 0;
        for (int i = 0; i < n && count < max_results; i++)
        {
            String host = MDNS.hostname(i);
            if (host.length() == 0) continue;
            NexaDiscoveredPlug& r = results[count];
            memset(r.hostname, 0, NEXA_HOST_LEN);
            host.toCharArray(r.hostname, NEXA_HOST_LEN);
            r.ip = MDNS.address(i);
            count++;
        }
        return count;
    }

    // Manual on/off from dashboard — sends HTTP immediately.
    bool forceToggle(const NexaCfg& cfg, int idx, bool on)
    {
        if (idx < 0 || idx >= cfg.num_plugs || idx >= MAX_NEXA_PLUGS)
            return false;
        const NexaPlugCfg& p = cfg.plugs[idx];
        if (p.hostname[0] == '\0') return false;
        plug_on[idx] = on;
        plug_reachable[idx] = sendState(cfg, idx, on);
        Log.info("[NEXA] Manual toggle plug %d (%s) -> %s%s", idx, p.name,
                 on ? "ON" : "OFF", plug_reachable[idx] ? "" : " (FAILED)");
        return plug_reachable[idx];
    }

    // Called from greenhouse update loop (~every 100 ms tick).
    // HTTP calls only happen on state changes or periodic retries.
    void update(const NexaCfg& cfg, bool is_dark, bool time_synced)
    {
        unsigned long now_ms = millis();
        bool periodic = (now_ms - m_last_retry_ms >= RETRY_INTERVAL_MS);

        // Priority: handle state changes (one plug per tick to avoid blocking)
        for (int i = 0; i < MAX_NEXA_PLUGS; i++)
        {
            const NexaPlugCfg& p = cfg.plugs[i];
            if (i >= cfg.num_plugs || !p.enabled || p.hostname[0] == '\0')
            {
                plug_on[i] = false;
                plug_reachable[i] = false;
                continue;
            }

            bool should_on = desiredState(p, is_dark, time_synced);
            if (should_on != plug_on[i])
            {
                plug_on[i] = should_on;
                plug_reachable[i] = sendState(cfg, i, should_on);
                Log.info("[NEXA] Plug %d (%s) -> %s%s", i, p.name,
                         should_on ? "ON" : "OFF",
                         plug_reachable[i] ? "" : " (FAILED)");
                return;  // One HTTP call per tick
            }
        }

        // Periodic: verify/retry one plug (round-robin)
        if (!periodic) return;
        m_last_retry_ms = now_ms;

        for (int j = 0; j < MAX_NEXA_PLUGS; j++)
        {
            int i = (m_rr_idx + 1 + j) % MAX_NEXA_PLUGS;
            const NexaPlugCfg& p = cfg.plugs[i];
            if (i < cfg.num_plugs && p.enabled && p.hostname[0] != '\0')
            {
                plug_reachable[i] = sendState(cfg, i, plug_on[i]);
                m_rr_idx = i;
                return;  // One HTTP call per tick
            }
        }
    }

private:
    unsigned long m_last_retry_ms       = 0;
    int           m_rr_idx              = -1;
    unsigned long m_last_browse_ms      = 0;

    // Cached resolved IPs (populated via mDNS browse, never expire by time)
    IPAddress     m_cached_ip[MAX_NEXA_PLUGS];

    static constexpr unsigned long RETRY_INTERVAL_MS     = 30000;
    static constexpr unsigned long BROWSE_COOLDOWN_MS    = 30000;  // min interval between mDNS browses
    static constexpr int           NEXA_PORT             = 3000;
    static constexpr int           TIMEOUT_MS            = 1500;

    bool desiredState(const NexaPlugCfg& p, bool is_dark, bool time_synced)
    {
        if (!is_dark || !time_synced) return false;
        time_t now = time(nullptr);
        struct tm* tm_now = localtime(&now);
        for (int s = 0; s < p.num_time_spans && s < MAX_TIME_SPANS; s++)
        {
            if (!p.time_spans[s].enabled) continue;
            if (isTimeInSpan(tm_now, p.time_spans[s])) return true;
        }
        return false;
    }

    // Resolve hostname → IP via mDNS service browse.
    // Updates cached IPs for ALL configured plugs found in one browse.
    // Rate-limited to avoid blocking the control loop too often.
    bool ensureIP(const NexaCfg& cfg, int idx)
    {
        if (m_cached_ip[idx] != IPAddress(0, 0, 0, 0))
            return true;

        unsigned long now_ms = millis();
        if (now_ms - m_last_browse_ms < BROWSE_COOLDOWN_MS)
            return false;
        m_last_browse_ms = now_ms;

        Log.info("[NEXA] mDNS browse for plug resolution...");
        int n = MDNS.queryService(NEXA_MDNS_SERVICE, NEXA_MDNS_PROTO);
        for (int i = 0; i < n; i++)
        {
            String host = MDNS.hostname(i);
            IPAddress ip = MDNS.address(i);
            for (int j = 0; j < cfg.num_plugs && j < MAX_NEXA_PLUGS; j++)
            {
                if (host.equalsIgnoreCase(cfg.plugs[j].hostname))
                    m_cached_ip[j] = ip;
            }
        }
        return m_cached_ip[idx] != IPAddress(0, 0, 0, 0);
    }

    // Send state to a plug. Resolves hostname via mDNS if needed.
    // On HTTP failure, invalidates the cached IP so next attempt re-resolves.
    bool sendState(const NexaCfg& cfg, int idx, bool on)
    {
        if (cfg.plugs[idx].hostname[0] == '\0') return false;
        if (!ensureIP(cfg, idx)) return false;

        bool ok = httpSetState(m_cached_ip[idx], on);
        if (!ok)
        {
            // IP may have changed (DHCP). Invalidate cache and retry once.
            m_cached_ip[idx] = IPAddress(0, 0, 0, 0);
            if (ensureIP(cfg, idx))
                ok = httpSetState(m_cached_ip[idx], on);
        }
        return ok;
    }

    bool httpSetState(IPAddress ip, bool on)
    {
        String url = "http://" + ip.toString() + ":" + String(NEXA_PORT)
                   + "/state?v=" + String(on ? 1 : 0);
        HTTPClient http;
        http.begin(url);
        http.setTimeout(TIMEOUT_MS);
        int code = http.GET();
        http.end();
        return (code == 200);
    }
};

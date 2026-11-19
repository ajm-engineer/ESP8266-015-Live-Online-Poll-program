#include <ESP8266WiFi.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <ESP8266mDNS.h>
extern "C" {
#include <user_interface.h>
}

// Settings
const char*    WIFI_SSID       = "";
const char*    WIFI_PASSWORD   = "";
const char*    LOCAL_NAME      = "livepoll";

const char*    AP_SSID         = "Live Poll";
const char*    AP_PASSWORD     = "";
const char*    GUEST_CODE      = "";
const char*    ADMIN_USER      = "admin";
const char*    ADMIN_PASS      = "changeme";    // change before use
const char*    PORTAL_TITLE    = "Live Poll";
const char*    WELCOME_NOTE    = "This Wi-Fi has no internet access.";
const uint32_t SESSION_MINUTES = 240;
const uint8_t  AP_CHANNEL      = 6;
const int      LED_PIN         = 2;

const bool     RELEASE_AFTER_SIGNIN = false;

const uint8_t  MAX_STATIONS = 8;
const uint8_t  MAX_SESSIONS = 16;
const uint8_t  MAX_OPTIONS  = 6;
const uint16_t MAX_VOTERS   = 200;
const uint32_t SESSION_MS   = SESSION_MINUTES * 60UL * 1000UL;
const char*    POLL_FILE    = "/poll.bin";
const uint32_t POLL_MAGIC   = 0x504F4C31;
const uint32_t WIFI_JOIN_TIMEOUT_MS = 20000;
const uint32_t ONLINE_WINDOW_MS     = 20000;

bool             apMode = true;
bool             wifiJoinFailed = false;
IPAddress        apIP(192, 168, 4, 1);
DNSServer        dnsServer;
ESP8266WebServer server(80);

struct GuestSession {
  bool      active;
  bool      hasMac;
  bool      online;
  uint8_t   mac[6];
  IPAddress ip;
  char      name[33];
  uint32_t  loginMs;
  uint32_t  lastSeenMs;
};

struct PollState {
  uint32_t id;
  bool     open;
  bool     showBeforeVote;
  uint8_t  optionCount;
  char     question[101];
  char     options[MAX_OPTIONS][41];
  uint16_t votes[MAX_OPTIONS];
};

struct Voter {
  uint8_t mac[6];
  uint8_t choice;
};

enum ProbeKind { PROBE_ANDROID, PROBE_APPLE, PROBE_WINDOWS, PROBE_WINDOWS_NCSI, PROBE_FIREFOX };

GuestSession sessions[MAX_SESSIONS];
PollState    pollState;
Voter        voters[MAX_VOTERS];
uint16_t     voterCount   = 0;
bool         fsReady      = false;
bool         pollDirty    = false;
uint32_t     dirtySinceMs = 0;
uint32_t     totalSignIns = 0;
uint32_t     lastScanMs   = 0;
uint32_t     ledOffAt     = 0;

static const char CSS[] PROGMEM = R"css(<style>
:root{--bg:#f3f4f6;--card:#fff;--ink:#111827;--mute:#6b7280;--line:#d1d5db;--acc:#2563eb;--bad:#dc2626;--ok:#16a34a}
@media(prefers-color-scheme:dark){:root{--bg:#0b0d12;--card:#161a22;--ink:#e5e7eb;--mute:#9ca3af;--line:#2b313c;--acc:#3b82f6;--bad:#ef4444;--ok:#22c55e}.fill{opacity:.38!important}}
*{box-sizing:border-box}
button,input{font-family:inherit}
body{margin:0;min-height:100vh;display:flex;align-items:flex-start;justify-content:center;padding:32px 16px;background:var(--bg);color:var(--ink);font:16px/1.45 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,sans-serif}
a{color:var(--acc)}
.card{width:100%;max-width:420px;background:var(--card);border:1px solid var(--line);border-radius:16px;padding:28px 24px}
.wide{max-width:720px}
.big{max-width:1000px}
h1{font-size:1.35rem;margin:0 0 6px}
h2{font-size:1.1rem;margin:28px 0 6px}
p{margin:0 0 16px;color:var(--mute)}
b{color:var(--ink)}
label{display:block;margin:14px 0 0;font-size:.85rem;color:var(--mute)}
input{width:100%;margin-top:6px;padding:12px 14px;font-size:1rem;color:var(--ink);background:var(--bg);border:1px solid var(--line);border-radius:10px}
input:focus{outline:2px solid var(--acc);border-color:transparent}
.check{display:flex;align-items:center;gap:10px;margin-top:18px;color:var(--ink);font-size:.95rem}
.check input{width:auto;margin:0}
button{width:100%;margin-top:20px;padding:13px;font-size:1rem;font-weight:600;color:#fff;background:var(--acc);border:0;border-radius:10px;cursor:pointer}
.row{display:flex;flex-wrap:wrap;gap:8px;margin-top:16px}
.row form{margin:0}
.row button,.btn{display:inline-block;width:auto;margin:0;padding:10px 14px;font-size:.9rem;font-weight:600;line-height:1.2;border:1px solid transparent;border-radius:10px;text-decoration:none;background:var(--acc);color:#fff}
.ghost,.row .ghost{background:transparent;color:var(--ink);border:1px solid var(--line)}
.danger,.row .danger{background:var(--bad);color:#fff}
.mini{width:auto;margin:0;padding:6px 12px;font-size:.8rem;background:var(--bad)}
.opt{position:relative;display:flex;justify-content:space-between;align-items:center;gap:12px;width:100%;margin:10px 0 0;padding:14px 16px;background:var(--bg);color:var(--ink);border:1px solid var(--line);border-radius:12px;font-weight:500;text-align:left;overflow:hidden}
.opt.mine{border-color:var(--acc);box-shadow:inset 0 0 0 1px var(--acc)}
.opt:disabled{cursor:default;color:var(--ink);opacity:1}
.fill{position:absolute;left:0;top:0;bottom:0;background:var(--acc);opacity:.22;transition:width .5s}
.lbl,.cnt{position:relative}
.cnt{color:var(--mute);font-size:.9rem;white-space:nowrap}
.big h1{font-size:2.4rem;margin:8px 0 18px}
.big .opt{font-size:1.5rem;padding:20px 22px}
.big .cnt{font-size:1.2rem}
.join{font-size:.85rem}
.big .join{font-size:1.35rem}
.err{margin-top:12px;color:var(--bad);font-size:.9rem}
.small{font-size:.8rem;color:var(--mute)}
.scroll{overflow-x:auto}
table{width:100%;border-collapse:collapse;font-size:.9rem}
th,td{padding:10px 6px;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}
th{font-size:.75rem;text-transform:uppercase;letter-spacing:.04em;color:var(--mute)}
.on{color:var(--ok)}.off{color:var(--mute)}
</style>)css";

String homeURL() { return apMode ? "http://" + apIP.toString() + "/" : String("/"); }

String siteIP() { return apMode ? apIP.toString() : WiFi.localIP().toString(); }

String siteAddressHtml() {
  if (apMode) return "<b>http://" + apIP.toString() + "</b>";
  return "<b>http://" + String(LOCAL_NAME) + ".local</b> or <b>http://" + siteIP() + "</b>";
}

String macStr(const uint8_t* m) {
  char b[18];
  snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", m[0], m[1], m[2], m[3], m[4], m[5]);
  return String(b);
}

String htmlEscape(const String& in) {
  String out;
  out.reserve(in.length() + 16);
  for (unsigned i = 0; i < in.length(); i++) {
    char c = in[i];
    switch (c) {
      case '&':  out += F("&amp;");  break;
      case '<':  out += F("&lt;");   break;
      case '>':  out += F("&gt;");   break;
      case '"':  out += F("&quot;"); break;
      case '\'': out += F("&#39;");  break;
      default:   out += c;
    }
  }
  return out;
}

String cleanText(String s, unsigned maxBytes) {
  s.trim();
  String out;
  for (unsigned i = 0; i < s.length() && out.length() < maxBytes; i++) {
    uint8_t c = (uint8_t)s[i];
    if (c >= 0x20 && c != 0x7F) out += (char)c;
  }
  int n = out.length();
  if (n > 0) {
    int start = n - 1;
    while (start > 0 && ((uint8_t)out[start] & 0xC0) == 0x80) start--;
    uint8_t lead = (uint8_t)out[start];
    int need = (lead < 0x80) ? 1 : (lead >= 0xF0) ? 4 : (lead >= 0xE0) ? 3 : 2;
    if (n - start < need) out.remove(start);
  }
  out.trim();
  return out;
}

String ago(uint32_t ms) {
  uint32_t m = ms / 60000UL;
  if (m < 1) return F("just now");
  if (m < 60) return String(m) + F(" min ago");
  return String(m / 60) + F(" h ") + String(m % 60) + F(" min ago");
}

void redirectTo(const String& url) {
  server.sendHeader("Location", url, true);
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(302, "text/plain", "");
}

void blinkLed() {
  if (LED_PIN < 0) return;
  digitalWrite(LED_PIN, LOW);
  ledOffAt = millis() + 200;
}

bool macForIP(const IPAddress& ip, uint8_t mac[6]) {
  if (!apMode) return false;
  bool found = false;
  struct station_info* list = wifi_softap_get_station_info();
  for (struct station_info* s = list; s != nullptr; s = STAILQ_NEXT(s, next)) {
    if (IPAddress(s->ip.addr) == ip) {
      memcpy(mac, s->bssid, 6);
      found = true;
      break;
    }
  }
  if (list) wifi_softap_free_station_info();
  return found;
}

int findSession(const IPAddress& ip) {
  uint8_t mac[6];
  bool haveMac = macForIP(ip, mac);
  for (int i = 0; i < MAX_SESSIONS; i++) {
    GuestSession& s = sessions[i];
    if (!s.active) continue;
    bool match = (haveMac && s.hasMac) ? memcmp(s.mac, mac, 6) == 0 : (s.ip == ip);
    if (!match) continue;
    if (millis() - s.loginMs > SESSION_MS) {
      s.active = false;
      return -1;
    }
    if (haveMac && !s.hasMac) { memcpy(s.mac, mac, 6); s.hasMac = true; }
    s.ip = ip;
    s.lastSeenMs = millis();
    return i;
  }
  return -1;
}

int claimSlot() {
  uint32_t now = millis();
  for (int pass = 0; pass < 2; pass++) {
    int pick = -1;
    uint32_t oldest = 0;
    for (int i = 0; i < MAX_SESSIONS; i++) {
      if (!sessions[i].active) return i;
      if (pass == 0 && sessions[i].online) continue;
      uint32_t age = now - sessions[i].loginMs;
      if (pick < 0 || age > oldest) { pick = i; oldest = age; }
    }
    if (pick >= 0) return pick;
  }
  return 0;
}

void refreshSessions() {
  uint32_t now = millis();
  struct station_info* list = apMode ? wifi_softap_get_station_info() : nullptr;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    GuestSession& s = sessions[i];
    if (!s.active) continue;
    if (now - s.loginMs > SESSION_MS) {
      Serial.printf("[expired]  %s\n", s.name);
      s.active = false;
      continue;
    }
    if (apMode) {
      s.online = false;
      for (struct station_info* p = list; p != nullptr; p = STAILQ_NEXT(p, next)) {
        bool match = s.hasMac ? memcmp(p->bssid, s.mac, 6) == 0 : IPAddress(p->ip.addr) == s.ip;
        if (match) { s.online = true; break; }
      }
      if (s.online) s.lastSeenMs = now;
    } else {

      s.online = (now - s.lastSeenMs) < ONLINE_WINDOW_MS;
    }
  }
  if (list) wifi_softap_free_station_info();
}

bool havePoll() { return pollState.optionCount >= 2; }

uint16_t totalVotes() {
  uint16_t t = 0;
  for (int i = 0; i < pollState.optionCount; i++) t += pollState.votes[i];
  return t;
}

void deviceId(const GuestSession& s, uint8_t out[6]) {
  if (s.hasMac) { memcpy(out, s.mac, 6); return; }
  out[0] = 0x02; out[1] = 0x00;
  for (int i = 0; i < 4; i++) out[2 + i] = s.ip[i];
}

int findVoter(const uint8_t dev[6]) {
  for (int i = 0; i < voterCount; i++) {
    if (memcmp(voters[i].mac, dev, 6) == 0) return i;
  }
  return -1;
}

void markDirty() {
  if (!pollDirty) dirtySinceMs = millis();
  pollDirty = true;
}

void savePoll() {
  pollDirty = false;
  if (!fsReady) return;
  File f = LittleFS.open(POLL_FILE, "w");
  if (!f) { Serial.println(F("[flash]    couldn't save the poll")); return; }
  uint32_t magic = POLL_MAGIC;
  uint16_t size  = sizeof(PollState);
  f.write((const uint8_t*)&magic, sizeof(magic));
  f.write((const uint8_t*)&size, sizeof(size));
  f.write((const uint8_t*)&pollState, sizeof(pollState));
  f.write((const uint8_t*)&voterCount, sizeof(voterCount));
  f.write((const uint8_t*)voters, sizeof(Voter) * voterCount);
  f.close();
}

bool loadPoll() {
  if (!fsReady || !LittleFS.exists(POLL_FILE)) return false;
  File f = LittleFS.open(POLL_FILE, "r");
  if (!f) return false;
  uint32_t magic = 0;
  uint16_t size = 0, count = 0;
  bool ok = f.read((uint8_t*)&magic, sizeof(magic)) == (int)sizeof(magic) && magic == POLL_MAGIC &&
            f.read((uint8_t*)&size, sizeof(size)) == (int)sizeof(size) && size == sizeof(PollState) &&
            f.read((uint8_t*)&pollState, sizeof(pollState)) == (int)sizeof(pollState) &&
            f.read((uint8_t*)&count, sizeof(count)) == (int)sizeof(count) && count <= MAX_VOTERS &&
            f.read((uint8_t*)voters, sizeof(Voter) * count) == (int)(sizeof(Voter) * count);
  f.close();
  if (ok && pollState.optionCount <= MAX_OPTIONS) {
    pollState.question[sizeof(pollState.question) - 1] = 0;
    for (int i = 0; i < MAX_OPTIONS; i++) pollState.options[i][sizeof(pollState.options[i]) - 1] = 0;
    voterCount = count;
    return true;
  }
  memset(&pollState, 0, sizeof(pollState));
  voterCount = 0;
  return false;
}

void makeDemoPoll() {
  memset(&pollState, 0, sizeof(pollState));
  strlcpy(pollState.question, "What should we get for dinner?", sizeof(pollState.question));
  const char* demo[] = {"Pizza", "Tacos", "Burgers", "Sushi"};
  for (const char* o : demo) {
    strlcpy(pollState.options[pollState.optionCount++], o, sizeof(pollState.options[0]));
  }
  pollState.open = true;
  pollState.id = 1;
  voterCount = 0;
}

String pageStart(const String& title, const char* cardClass = "card", int refreshSec = 0) {
  String p;
  p.reserve(4096);
  p += F("<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\">"
         "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">");
  if (refreshSec > 0) {
    p += F("<meta http-equiv=\"refresh\" content=\"");
    p += String(refreshSec);
    p += F("\">");
  }
  p += F("<title>");
  p += htmlEscape(title);
  p += F("</title>");
  p += FPSTR(CSS);
  p += F("</head><body><div class=\"");
  p += cardClass;
  p += F("\">");
  return p;
}

void sendPage(String& p) {
  p += F("</div></body></html>");
  server.sendHeader("Cache-Control", "no-cache, no-store, must-revalidate");
  server.send(200, "text/html", p);
}

String votesLabel(uint16_t n) {
  return String(n) + (n == 1 ? F(" vote") : F(" votes"));
}

int percentOf(uint16_t v, uint16_t total) {
  return total ? (v * 100 + total / 2) / total : 0;
}

void addOptions(String& p, int mine, bool showCounts, bool asButtons, bool canVote) {
  uint16_t total = totalVotes();
  for (int i = 0; i < pollState.optionCount; i++) {
    uint16_t v = pollState.votes[i];
    p += asButtons ? F("<button type=\"submit\" name=\"c\" value=\"") : F("<div data-i=\"");
    p += String(i);
    p += F("\" id=\"o");
    p += String(i);
    p += F("\" class=\"opt");
    if (i == mine) p += F(" mine");
    p += '"';
    if (asButtons && !canVote) p += F(" disabled");
    p += '>';
    if (showCounts) {
      p += F("<span class=\"fill\" style=\"width:");
      p += String(percentOf(v, total));
      p += F("%\"></span>");
    }
    p += F("<span class=\"lbl\">");
    p += htmlEscape(pollState.options[i]);
    if (i == mine) p += F(" &#10003;");
    p += F("</span>");
    if (showCounts) {
      p += F("<span class=\"cnt\">");
      p += String(v);
      p += F(" &middot; ");
      p += String(percentOf(v, total));
      p += F("%</span>");
    }
    p += asButtons ? F("</button>") : F("</div>");
  }
}

void addLiveScript(String& p, const char* url, bool shown) {
  p += F("<script>(function(){var id=");
  p += String(pollState.id);
  p += F(",open=");
  p += pollState.open ? F("true") : F("false");
  p += F(",n=");
  p += String(pollState.optionCount);
  p += F(",show=");
  p += shown ? F("true") : F("false");
  p += F(";function tick(){fetch('");
  p += url;
  p += F("',{cache:'no-store'}).then(function(r){return r.json()}).then(function(d){"
         "if(d.id!==id||d.open!==open||d.n!==n||d.show!==show){location.reload();return}"
         "if(!d.show)return;var t=0,i,b,v,pc;for(i=0;i<d.votes.length;i++)t+=d.votes[i];"
         "for(i=0;i<d.votes.length;i++){b=document.getElementById('o'+i);if(!b)continue;"
         "v=d.votes[i];pc=t?Math.round(v*100/t):0;"
         "b.querySelector('.fill').style.width=pc+'%';"
         "b.querySelector('.cnt').textContent=v+' \\u00b7 '+pc+'%'}"
         "var e=document.getElementById('tot');if(e)e.textContent=t+(t==1?' vote':' votes')"
         "}).catch(function(){})}setInterval(tick,2500)})();</script>");
}

void sendPollJson(int mine, bool forceShow) {
  bool show = havePoll() && (forceShow || mine >= 0 || pollState.showBeforeVote || !pollState.open);
  String j;
  j.reserve(160);
  j += F("{\"id\":");
  j += String(pollState.id);
  j += F(",\"open\":");
  j += pollState.open ? F("true") : F("false");
  j += F(",\"n\":");
  j += String(pollState.optionCount);
  j += F(",\"show\":");
  j += show ? F("true") : F("false");
  if (show) {
    j += F(",\"votes\":[");
    for (int i = 0; i < pollState.optionCount; i++) {
      if (i) j += ',';
      j += String(pollState.votes[i]);
    }
    j += ']';
  }
  j += '}';
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", j);
}

void sendLoginPage(const String& error = String(), const String& name = String()) {
  String p = pageStart(PORTAL_TITLE);
  p += F("<h1>");
  p += htmlEscape(PORTAL_TITLE);
  p += F("</h1><p>Enter your name to join the poll.</p>"
         "<form method=\"post\" action=\"/login\">"
         "<label for=\"n\">Your name</label>"
         "<input id=\"n\" name=\"name\" maxlength=\"32\" autocomplete=\"name\" required value=\"");
  p += htmlEscape(name);
  p += F("\">");
  if (strlen(GUEST_CODE) > 0) {
    p += F("<label for=\"c\">Access code</label>"
           "<input id=\"c\" name=\"code\" type=\"password\" autocomplete=\"off\" required>");
  }
  if (error.length()) {
    p += F("<div class=\"err\">");
    p += htmlEscape(error);
    p += F("</div>");
  }
  p += F("<button type=\"submit\">Join</button></form>");
  sendPage(p);
}

void sendPollPage(const GuestSession& s, const String& note = String()) {
  uint8_t dev[6];
  deviceId(s, dev);
  int vi = findVoter(dev);
  int mine = (vi >= 0) ? voters[vi].choice : -1;
  bool show = havePoll() && (mine >= 0 || pollState.showBeforeVote || !pollState.open);

  String p = pageStart(PORTAL_TITLE);
  p += F("<p class=\"small\">Hi, <b>");
  p += htmlEscape(s.name);
  p += F("</b></p>");
  if (!havePoll()) {
    p += F("<h1>No poll right now</h1><p>Hang tight. This page updates on its own.</p>");
  } else {
    p += F("<h1>");
    p += htmlEscape(pollState.question);
    p += F("</h1><p class=\"small\">");
    if (!pollState.open)  p += F("Voting is closed. Final results:");
    else if (mine >= 0)   p += F("Thanks for voting! Tap a different answer to change your vote.");
    else                  p += F("Tap an answer to vote.");
    p += F("</p>");
    if (note.length()) {
      p += F("<div class=\"err\">");
      p += htmlEscape(note);
      p += F("</div>");
    }
    p += F("<form method=\"post\" action=\"/vote\">");
    addOptions(p, mine, show, true, pollState.open);
    p += F("</form>");
    if (show) {
      p += F("<p class=\"small\" id=\"tot\" style=\"margin-top:12px\">");
      p += votesLabel(totalVotes());
      p += F("</p>");
    }
  }
  p += F("<p class=\"small\" style=\"margin-top:24px\">");
  if (apMode) {
    p += htmlEscape(WELCOME_NOTE);
    p += ' ';
  }
  p += F("To get back to this page later, open ");
  p += siteAddressHtml();
  p += F("</p><form method=\"post\" action=\"/logout\">"
         "<button class=\"ghost\" type=\"submit\">Sign out</button></form>");
  addLiveScript(p, "/poll.json", show);
  sendPage(p);
}

void handleRoot() {

  if (apMode && server.hostHeader() != apIP.toString()) { redirectTo(homeURL()); return; }
  int i = findSession(server.client().remoteIP());
  if (i >= 0) sendPollPage(sessions[i]);
  else sendLoginPage();
}

void handleLogin() {
  String name = cleanText(server.arg("name"), 32);
  if (name.length() == 0) { sendLoginPage(F("Please enter your name.")); return; }
  if (strlen(GUEST_CODE) > 0 && server.arg("code") != GUEST_CODE) {
    Serial.printf("[bad code] from %s\n", server.client().remoteIP().toString().c_str());
    sendLoginPage(F("That access code isn't right."), name);
    return;
  }

  IPAddress ip = server.client().remoteIP();
  int i = findSession(ip);
  if (i < 0) i = claimSlot();
  GuestSession& s = sessions[i];
  s.active = true;
  s.online = true;
  s.ip = ip;
  s.hasMac = macForIP(ip, s.mac);
  strlcpy(s.name, name.c_str(), sizeof(s.name));
  s.loginMs = s.lastSeenMs = millis();
  totalSignIns++;

  Serial.printf("[sign-in]  %s  %s  %s\n", s.name, ip.toString().c_str(),
                s.hasMac ? macStr(s.mac).c_str() : "mac?");
  blinkLed();
  redirectTo(homeURL());
}

void handleLogout() {
  int i = findSession(server.client().remoteIP());
  if (i >= 0) {
    Serial.printf("[sign-out] %s\n", sessions[i].name);
    sessions[i].active = false;
  }
  redirectTo(homeURL());
}

void handleVote() {
  int si = findSession(server.client().remoteIP());
  if (si < 0 || !havePoll() || !pollState.open || !server.hasArg("c")) { redirectTo(homeURL()); return; }
  int c = server.arg("c").toInt();
  if (c < 0 || c >= pollState.optionCount) { redirectTo(homeURL()); return; }

  uint8_t dev[6];
  deviceId(sessions[si], dev);
  int vi = findVoter(dev);
  if (vi >= 0) {
    uint8_t old = voters[vi].choice;
    if (old == c) { redirectTo(homeURL()); return; }
    if (old < pollState.optionCount && pollState.votes[old] > 0) pollState.votes[old]--;
    voters[vi].choice = c;
  } else if (voterCount < MAX_VOTERS) {
    memcpy(voters[voterCount].mac, dev, 6);
    voters[voterCount].choice = c;
    voterCount++;
  } else {
    sendPollPage(sessions[si], F("Sorry, this poll has reached its vote limit."));
    return;
  }
  pollState.votes[c]++;
  markDirty();

  Serial.printf("[vote]     %s -> %s\n", sessions[si].name, pollState.options[c]);
  blinkLed();
  redirectTo(homeURL());
}

void handlePollJson() {
  int si = findSession(server.client().remoteIP());
  if (si < 0) {
    server.send(200, "application/json", "{\"id\":-1}");
    return;
  }
  uint8_t dev[6];
  deviceId(sessions[si], dev);
  int vi = findVoter(dev);
  sendPollJson(vi >= 0 ? voters[vi].choice : -1, false);
}

void handleProbe(ProbeKind kind) {
  if (!apMode || !RELEASE_AFTER_SIGNIN || findSession(server.client().remoteIP()) < 0) {
    redirectTo(homeURL());
    return;
  }
  switch (kind) {
    case PROBE_ANDROID:      server.send(204); break;
    case PROBE_APPLE:        server.send(200, "text/html", "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>"); break;
    case PROBE_WINDOWS:      server.send(200, "text/plain", "Microsoft Connect Test"); break;
    case PROBE_WINDOWS_NCSI: server.send(200, "text/plain", "Microsoft NCSI"); break;
    case PROBE_FIREFOX:      server.send(200, "text/plain", "success\n"); break;
  }
}

void handleNotFound() { redirectTo(homeURL()); }

String joinInstructionsHtml() {
  if (apMode) return "Join the <b>" + htmlEscape(AP_SSID) + "</b> Wi-Fi to vote";
  return "On the <b>" + htmlEscape(WIFI_SSID) + "</b> Wi-Fi, go to " + siteAddressHtml() + " to vote";
}

String networkSummaryHtml() {
  if (!apMode) {
    return "On Wi-Fi <b>" + htmlEscape(WIFI_SSID) + "</b> (signal " + String((int)WiFi.RSSI()) +
           " dBm). Guests open " + siteAddressHtml() + ".";
  }
  String s = "Running its own network <b>" + htmlEscape(AP_SSID) + "</b>. Guests join it and open " +
             siteAddressHtml() + ".";
  if (wifiJoinFailed) s += " (Couldn't join <b>" + htmlEscape(WIFI_SSID) + "</b> at startup.)";
  return s;
}

bool adminAuthed() {
  if (server.authenticate(ADMIN_USER, ADMIN_PASS)) return true;
  server.requestAuthentication(BASIC_AUTH, "Portal admin");
  return false;
}

String actionButton(const char* url, const char* label, const char* cls, const char* confirmMsg = nullptr) {
  String b = F("<form method=\"post\" action=\"");
  b += url;
  b += F("\"><button type=\"submit\" class=\"");
  b += cls;
  b += '"';
  if (confirmMsg) {
    b += F(" onclick=\"return confirm('");
    b += confirmMsg;
    b += F("')\"");
  }
  b += '>';
  b += label;
  b += F("</button></form>");
  return b;
}

void handleAdmin() {
  if (!adminAuthed()) return;
  refreshSessions();
  uint32_t now = millis();

  String p = pageStart(F("Portal admin"), "card wide", 10);

  p += F("<p class=\"small\">");
  p += networkSummaryHtml();
  p += F("</p>");

  p += F("<h1>Poll</h1>");
  if (!havePoll()) {
    p += F("<p>No poll yet.</p>");
  } else {
    p += F("<p><b>");
    p += htmlEscape(pollState.question);
    p += F("</b><br><span class=\"small\">");
    p += pollState.open ? F("Voting open") : F("Voting closed");
    p += F(" &middot; ");
    p += votesLabel(totalVotes());
    p += F(" &middot; ");
    p += pollState.showBeforeVote ? F("results visible to everyone") : F("results shown after voting");
    p += F("</span></p>");
    addOptions(p, -1, true, false, false);
  }
  p += F("<div class=\"row\"><a class=\"btn\" href=\"/admin/new\">New poll</a>");
  if (havePoll()) {
    p += F("<a class=\"btn ghost\" href=\"/admin/screen\" target=\"_blank\">Big screen</a>");
    p += actionButton("/admin/poll/toggle", pollState.open ? "Close voting" : "Reopen voting", "ghost");
    p += actionButton("/admin/poll/show",
                      pollState.showBeforeVote ? "Hide results until voted" : "Show results to everyone", "ghost");
    p += actionButton("/admin/poll/reset", "Reset votes", "danger", "Reset all votes?");
  }
  p += F("</div>");

  p += F("<h2>Signed-in guests</h2><p class=\"small\">");
  if (apMode) {
    p += String(WiFi.softAPgetStationNum());
    p += F(" device(s) connected &middot; ");
  }
  p += String(totalSignIns);
  p += F(" sign-ins since boot &middot; up ");
  p += String(now / 60000UL);
  p += F(" min &middot; ");
  p += String(ESP.getFreeHeap());
  p += F(" bytes free</p><div class=\"scroll\"><table>"
         "<tr><th>Name</th><th>Device</th><th>Signed in</th><th>Status</th><th></th></tr>");

  int count = 0;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    const GuestSession& s = sessions[i];
    if (!s.active) continue;
    count++;
    p += F("<tr><td>");
    p += htmlEscape(s.name);
    p += F("</td><td>");
    p += s.ip.toString();
    p += F("<br><span class=\"small\">");
    if (s.hasMac) p += macStr(s.mac);
    p += F("</span></td><td>");
    p += ago(now - s.loginMs);
    p += F("</td><td>");
    if (s.online) {
      p += F("<span class=\"on\">&#9679; online</span>");
    } else {
      p += F("<span class=\"off\">&#9675; left ");
      p += ago(now - s.lastSeenMs);
      p += F("</span>");
    }

    p += F("</td><td><form method=\"post\" action=\"/admin/kick\">"
           "<input type=\"hidden\" name=\"i\" value=\"");
    p += String(i);
    p += F("\"><input type=\"hidden\" name=\"t\" value=\"");
    p += String(s.loginMs);
    p += F("\"><button class=\"mini\" type=\"submit\">Kick</button></form></td></tr>");
  }
  if (count == 0) p += F("<tr><td colspan=\"5\" class=\"small\">Nobody has signed in yet.</td></tr>");
  p += F("</table></div>");
  if (count > 0) {
    p += F("<div class=\"row\">");
    p += actionButton("/admin/clear", "Sign everyone out", "danger", "Sign everyone out?");
    p += F("</div>");
  }
  p += F("<p class=\"small\" style=\"margin:16px 0 0\">This page refreshes every 10 seconds.</p>");
  sendPage(p);
}

void sendNewPollPage(const String& error = String()) {
  String p = pageStart(F("New poll"));
  p += F("<p class=\"small\"><a href=\"/admin\">&larr; Back to admin</a></p>"
         "<h1>New poll</h1><p>Starting a new poll clears all votes.</p>"
         "<form method=\"post\" action=\"/admin/poll/new\">"
         "<label>Question<input name=\"q\" maxlength=\"100\" required value=\"");
  p += htmlEscape(pollState.question);
  p += F("\"></label>");
  for (int i = 0; i < MAX_OPTIONS; i++) {
    p += F("<label>Answer ");
    p += String(i + 1);
    if (i >= 2) p += F(" (optional)");
    p += F("<input name=\"o");
    p += String(i);
    p += F("\" maxlength=\"40\"");
    if (i < 2) p += F(" required");
    p += F(" value=\"");
    if (i < pollState.optionCount) p += htmlEscape(pollState.options[i]);
    p += F("\"></label>");
  }
  p += F("<label class=\"check\"><input type=\"checkbox\" name=\"show\"");
  if (pollState.showBeforeVote) p += F(" checked");
  p += F(">Show results before people vote</label>");
  if (error.length()) {
    p += F("<div class=\"err\">");
    p += htmlEscape(error);
    p += F("</div>");
  }
  p += F("<button type=\"submit\">Start poll</button></form>");
  sendPage(p);
}

void handleNewPollPage() {
  if (!adminAuthed()) return;
  sendNewPollPage();
}

void handlePollNew() {
  if (!adminAuthed()) return;
  PollState next;
  memset(&next, 0, sizeof(next));
  String q = cleanText(server.arg("q"), 100);
  strlcpy(next.question, q.c_str(), sizeof(next.question));
  for (int i = 0; i < MAX_OPTIONS; i++) {
    String o = cleanText(server.arg("o" + String(i)), 40);
    if (o.length()) strlcpy(next.options[next.optionCount++], o.c_str(), sizeof(next.options[0]));
  }
  if (q.length() == 0 || next.optionCount < 2) {
    sendNewPollPage(F("Add a question and at least two answers."));
    return;
  }
  next.open = true;
  next.showBeforeVote = server.hasArg("show");
  next.id = pollState.id + 1;
  pollState = next;
  voterCount = 0;
  markDirty();
  Serial.printf("[poll]     new: %s\n", pollState.question);
  redirectTo("/admin");
}

void handlePollToggle() {
  if (!adminAuthed()) return;
  pollState.open = !pollState.open;
  markDirty();
  Serial.printf("[poll]     voting %s\n", pollState.open ? "opened" : "closed");
  redirectTo("/admin");
}

void handlePollShow() {
  if (!adminAuthed()) return;
  pollState.showBeforeVote = !pollState.showBeforeVote;
  markDirty();
  redirectTo("/admin");
}

void handlePollReset() {
  if (!adminAuthed()) return;
  memset(pollState.votes, 0, sizeof(pollState.votes));
  voterCount = 0;
  pollState.id++;
  markDirty();
  Serial.println(F("[poll]     votes reset"));
  redirectTo("/admin");
}

void handleScreen() {
  if (!adminAuthed()) return;
  String p = pageStart(F("Live results"), "card big");
  p += F("<p class=\"join\">");
  p += joinInstructionsHtml();
  p += F("</p>");
  if (!havePoll()) {
    p += F("<h1>No poll running</h1>");
  } else {
    p += F("<h1>");
    p += htmlEscape(pollState.question);
    p += F("</h1>");
    addOptions(p, -1, true, false, false);
    p += F("<p class=\"small\" id=\"tot\" style=\"margin-top:14px\">");
    p += votesLabel(totalVotes());
    p += F("</p>");
    if (!pollState.open) p += F("<p><b>Voting is closed.</b></p>");
  }
  addLiveScript(p, "/admin/poll.json", havePoll());
  sendPage(p);
}

void handleAdminPollJson() {
  if (!adminAuthed()) return;
  sendPollJson(-1, true);
}

void handleKick() {
  if (!adminAuthed()) return;
  int i = server.arg("i").toInt();
  uint32_t token = strtoul(server.arg("t").c_str(), nullptr, 10);
  if (i >= 0 && i < MAX_SESSIONS && sessions[i].active && sessions[i].loginMs == token) {
    Serial.printf("[kicked]   %s\n", sessions[i].name);
    sessions[i].active = false;
  }
  redirectTo("/admin");
}

void handleClear() {
  if (!adminAuthed()) return;
  for (int i = 0; i < MAX_SESSIONS; i++) sessions[i].active = false;
  Serial.println(F("[admin]    signed everyone out"));
  redirectTo("/admin");
}

bool joinWifi() {
  if (strlen(WIFI_SSID) == 0) return false;
  WiFi.mode(WIFI_STA);
  WiFi.hostname(LOCAL_NAME);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.printf("Joining Wi-Fi \"%s\"", WIFI_SSID);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < WIFI_JOIN_TIMEOUT_MS) {
    if (LED_PIN >= 0) digitalWrite(LED_PIN, !digitalRead(LED_PIN));
    delay(250);
    Serial.print('.');
  }
  if (LED_PIN >= 0) digitalWrite(LED_PIN, HIGH);

  wl_status_t st = WiFi.status();
  if (st == WL_CONNECTED) {
    Serial.printf(" connected, IP %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println();
  if (st == WL_NO_SSID_AVAIL)       Serial.println(F("Couldn't find that network. Check the name, and that it has a 2.4 GHz band."));
  else if (st == WL_WRONG_PASSWORD) Serial.println(F("Wrong Wi-Fi password."));
  else                              Serial.println(F("Couldn't join the Wi-Fi."));
  Serial.println(F("Starting its own network instead."));
  wifiJoinFailed = true;
  WiFi.disconnect(true);
  return false;
}

void startOwnNetwork() {
  apMode = true;
  bool secured = strlen(AP_PASSWORD) >= 8;
  if (strlen(AP_PASSWORD) > 0 && !secured) {
    Serial.println(F("AP_PASSWORD is shorter than 8 characters, so the network will be OPEN."));
  }
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  WiFi.softAP(AP_SSID, secured ? AP_PASSWORD : nullptr, AP_CHANNEL, 0, MAX_STATIONS);

  dnsServer.setErrorReplyCode(DNSReplyCode::NoError);
  dnsServer.setTTL(30);
  dnsServer.start(53, "*", apIP);
  Serial.printf("Running its own network \"%s\" (%s)\n", AP_SSID, secured ? "WPA2" : "open");
}

void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println();

  if (LED_PIN >= 0) { pinMode(LED_PIN, OUTPUT); digitalWrite(LED_PIN, HIGH); }

  fsReady = LittleFS.begin();
  if (!fsReady) Serial.println(F("No flash storage (Flash Size has FS:none?), so the poll won't survive a reboot."));
  if (loadPoll()) {
    Serial.printf("Loaded poll: %s (%u votes)\n", pollState.question, (unsigned)totalVotes());
  } else {
    makeDemoPoll();
    markDirty();
  }

  WiFi.persistent(false);
  if (joinWifi()) {
    apMode = false;
    if (MDNS.begin(LOCAL_NAME)) MDNS.addService("http", "tcp", 80);
    else Serial.println(F("mDNS didn't start, so use the IP address instead."));
  } else {
    startOwnNetwork();
  }

  server.on("/",                  handleRoot);
  server.on("/login",             HTTP_POST, handleLogin);
  server.on("/logout",            HTTP_POST, handleLogout);
  server.on("/vote",              HTTP_POST, handleVote);
  server.on("/poll.json",         HTTP_GET,  handlePollJson);

  server.on("/admin",             HTTP_GET,  handleAdmin);
  server.on("/admin/new",         HTTP_GET,  handleNewPollPage);
  server.on("/admin/screen",      HTTP_GET,  handleScreen);
  server.on("/admin/poll.json",   HTTP_GET,  handleAdminPollJson);
  server.on("/admin/poll/new",    HTTP_POST, handlePollNew);
  server.on("/admin/poll/toggle", HTTP_POST, handlePollToggle);
  server.on("/admin/poll/show",   HTTP_POST, handlePollShow);
  server.on("/admin/poll/reset",  HTTP_POST, handlePollReset);
  server.on("/admin/kick",        HTTP_POST, handleKick);
  server.on("/admin/clear",       HTTP_POST, handleClear);

  server.on("/generate_204",              [] { handleProbe(PROBE_ANDROID); });
  server.on("/gen_204",                   [] { handleProbe(PROBE_ANDROID); });
  server.on("/hotspot-detect.html",       [] { handleProbe(PROBE_APPLE); });
  server.on("/library/test/success.html", [] { handleProbe(PROBE_APPLE); });
  server.on("/connecttest.txt",           [] { handleProbe(PROBE_WINDOWS); });
  server.on("/ncsi.txt",                  [] { handleProbe(PROBE_WINDOWS_NCSI); });
  server.on("/success.txt",               [] { handleProbe(PROBE_FIREFOX); });

  server.onNotFound(handleNotFound);
  server.begin();

  Serial.printf("Live poll running\n  Poll:  http://%s/\n", siteIP().c_str());
  if (!apMode) Serial.printf("         http://%s.local/\n", LOCAL_NAME);
  Serial.printf("  Admin: http://%s/admin\n", siteIP().c_str());
}

void loop() {
  if (apMode) dnsServer.processNextRequest();
  else MDNS.update();
  server.handleClient();

  uint32_t now = millis();
  if (now - lastScanMs > 5000) {
    lastScanMs = now;
    refreshSessions();
  }
  if (pollDirty && now - dirtySinceMs > 3000) savePoll();
  if (ledOffAt && (int32_t)(now - ledOffAt) >= 0) {
    digitalWrite(LED_PIN, HIGH);
    ledOffAt = 0;
  }
}

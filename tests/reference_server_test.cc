#include "service/reference_server.h"

#include "platform/platform.h"

#include <algorithm>
#include <arpa/inet.h>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace {

using namespace seethis::core;

int checks = 0;
void Check(bool condition, std::string_view message) {
  ++checks;
  if (!condition) throw std::runtime_error(std::string(message));
}

struct TempDirectory {
  std::filesystem::path path;
  TempDirectory() {
    std::string pattern = "/tmp/seethis-server-test-XXXXXX";
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    char* result = mkdtemp(buffer.data());
    if (!result) throw std::runtime_error("mkdtemp failed");
    path = result;
  }
  ~TempDirectory() { std::filesystem::remove_all(path); }
};

Bytes FileBytes(const std::filesystem::path& path) {
  std::ifstream input(path,std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

Context ContextFixture(Stamp observed, int width = 100, int height = 100) {
  Context context;
  context.pid = 42;
  context.window_pid = 42;
  context.self_pid = 99;
  context.window_id = 7;
  context.app_name = "Fixture";
  context.bundle_id = "local.fixture";
  context.executable = "/fixture";
  context.window_title = "Immutable fixture";
  context.selection_method =
      "frontmost-pid/topmost-normal-containing-initial-point";
  context.window_layer = 0;
  context.window = {0, 0, static_cast<double>(width),
                    static_cast<double>(height)};
  context.quartz_to_appkit_top = height;
  context.observed = observed;
  context.displays.push_back(
      {"display-a", 1, {0, 0, static_cast<double>(width),
                         static_cast<double>(height)},
       {0, 0, static_cast<double>(width), static_cast<double>(height)}, 1,
       {1, -1, 0, static_cast<double>(height)}});
  context.displays.push_back(
      {"display-b", 2, {static_cast<double>(width),
                         -static_cast<double>(height) / 2,
                         static_cast<double>(width) / 2,
                         static_cast<double>(height) / 2},
       {0, 0, static_cast<double>(width),
        static_cast<double>(height)}, 2,
       {2, -2, -2 * static_cast<double>(width), 0}});
  context.excluded_window_ids = {8};
  context.exclusion_method =
      "exact-window-allowlist;self-pid-rejected;child-windows-off";
  return context;
}

WindowObservation WindowFor(const Context& context,
                            std::uint64_t generation) {
  WindowObservation observation;
  observation.availability = WindowAvailability::kReady;
  observation.generation = generation;
  observation.observed = Now();
  observation.pid = context.window_pid;
  observation.bundle_id = context.bundle_id;
  observation.window_id = context.window_id;
  observation.bounds = context.window;
  return observation;
}

void Observe(MarkController& marks, const Context& context) {
  marks.SetWindowObservation(
      WindowFor(context, marks.window_observation().generation + 1));
}

StoredReference MakeReference(ReferenceStore& store, const std::string& id,
                              std::int64_t age_seconds,
                              bool realistically_large = false,
                              bool multi_region = false) {
  const auto now = Now();
  Stamp observed{now.utc_us - age_seconds * 1'000'000LL,
                 now.monotonic_us - age_seconds * 1'000'000LL};
  MarkController marks(store, [](const auto&) { return true; });
  const auto generation = marks.Begin(id);
  const int width = realistically_large ? 2048 : 100;
  const int height = realistically_large ? 2400 : 100;
  Check(marks.BindContext(generation, ContextFixture(observed, width, height)),
        "fixture context binds");
  CaptureResult capture;
  capture.requested = {observed.utc_us + 10, observed.monotonic_us + 10};
  capture.completed = {observed.utc_us + 20, observed.monotonic_us + 20};
  capture.pixels = {static_cast<std::uint32_t>(width),
                    static_cast<std::uint32_t>(height),
                    Bytes(static_cast<std::size_t>(width) * height * 4)};
  std::uint32_t state = 0x9e3779b9U;
  for (std::size_t index = 0; index < capture.pixels.rgba.size(); ++index) {
    state = state * 1664525U + 1013904223U;
    capture.pixels.rgba[index] =
        index % 4 == 3 ? 255 : static_cast<std::uint8_t>(state >> 24);
  }
  marks.Captured(generation, std::move(capture));
  if(multi_region) {
    std::vector<InteractionSnapshot::LogicalRegion> regions;
    regions.push_back({{{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1},
                        {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}},
                       {{1, CoordinateUnit::kLogicalPoints, {30, 30}, 1},
                        {1, CoordinateUnit::kLogicalPoints, {50, 50}, 1}}});
    regions.push_back({{{1, CoordinateUnit::kLogicalPoints, {60, 60}, 1},
                        {1, CoordinateUnit::kLogicalPoints, {80, 80}, 1}}});
    marks.Release(generation, std::move(regions),
                  {observed.utc_us + 30, observed.monotonic_us + 30});
  }
  else marks.Release(generation,
      {{1,CoordinateUnit::kLogicalPoints,{20,20},1},
       {1,CoordinateUnit::kLogicalPoints,{40,40},1}},
      {observed.utc_us+30,observed.monotonic_us+30});
  store.WaitForIdleForTesting();
  if (marks.phase() != ReferencePhase::kCommitted) {
    const auto job = store.LookupMetadata(id);
    std::cerr << "Reference fixture failure: id=" << id << " age_seconds=" << age_seconds
              << " now_monotonic_us=" << now.monotonic_us
              << " observed_monotonic_us=" << observed.monotonic_us
              << " phase=" << static_cast<int>(marks.phase()) << " status=" << marks.status()
              << " job_state=" << (job ? static_cast<int>(job->state) : -1)
              << " code=" << (job ? job->code : "missing") << '\n';
  }
  Check(marks.phase() == ReferencePhase::kCommitted, "fixture commits");
  return *store.Lookup(id)->ready;
}

struct Response {
  int status = 0;
  std::string bytes;
  std::string body;
};

Response Request(std::uint16_t port, const std::string& request) {
  const int client = socket(AF_INET, SOCK_STREAM, 0);
  if (client < 0) throw std::runtime_error("socket failed");
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = htons(port);
  if (connect(client, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
    close(client);
    throw std::runtime_error("connect failed");
  }
  std::size_t sent = 0;
  while (sent < request.size()) {
    const auto count = send(client, request.data() + sent, request.size() - sent, 0);
    if (count <= 0) throw std::runtime_error("send failed");
    sent += static_cast<std::size_t>(count);
  }
  shutdown(client, SHUT_WR);
  Response response;
  char buffer[4096];
  for (;;) {
    const auto count = recv(client, buffer, sizeof(buffer), 0);
    if (count <= 0) break;
    response.bytes.append(buffer, static_cast<std::size_t>(count));
  }
  close(client);
  const auto first_space = response.bytes.find(' ');
  if (first_space != std::string::npos)
    response.status = std::stoi(response.bytes.substr(first_space + 1, 3));
  const auto boundary = response.bytes.find("\r\n\r\n");
  if (boundary != std::string::npos) response.body = response.bytes.substr(boundary + 4);
  return response;
}

std::string Header(const Response& response,std::string_view name) {
  const std::string prefix=std::string(name)+": ";
  const auto begin=response.bytes.find(prefix);
  if(begin==std::string::npos||begin>response.bytes.find("\r\n\r\n"))return {};
  const auto value=begin+prefix.size();
  const auto end=response.bytes.find("\r\n",value);
  return end==std::string::npos?std::string{}:response.bytes.substr(value,end-value);
}

void RunEmbeddedViewerCanvasHarness(std::string_view viewer_script) {
  TempDirectory temp;
  const auto viewer_path = temp.path / "reference-view.js";
  const auto harness_path = temp.path / "canvas-harness.js";
  {
    std::ofstream viewer(viewer_path);
    viewer << viewer_script;
  }
  {
    std::ofstream harness(harness_path);
    harness << R"JS((async () => {
const fs = require("fs");
const vm = require("vm");
const nodeCrypto = require("crypto");
globalThis.crypto = nodeCrypto.webcrypto;
const viewer = fs.readFileSync(process.argv[2], "utf8");
const id = "0123456789abcdef0123456789abcdef";
const capability = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789";
const png = new Uint8Array([137, 80, 78, 71, 13, 10, 26, 10, 0, 1, 2, 3]);
const pngDigest = nodeCrypto.createHash("sha256").update(png).digest("hex");
const point = (x, y) => ({display_id: 1, unit: "logical_points", x, y, backing_scale: 1});
const baseMetadata = () => ({
  schema: 2, reference_id: id, historical: true,
  context: {displays: [{id: 1, uuid: "synthetic-display", scale: 1,
    logical: {x: 0, y: 0, width: 120, height: 100},
    pixels: {x: 0, y: 0, width: 120, height: 100}}]},
  timestamps: {}, selection: {
    global_to_source: {a: 1, d: 1, tx: 0, ty: 0}, regions: [
      {region: {x: 10, y: 10, width: 25, height: 20},
       crop_pixels: {x: 10, y: 10, width: 25, height: 20},
       subpaths: [[point(10, 10), point(20, 20), point(25, 22)],
                  [point(30, 70), point(35, 75)]]},
      {region: {x: 80, y: 40, width: 20, height: 20},
       crop_pixels: {x: 80, y: 40, width: 20, height: 20},
       subpaths: [[point(80, 40), point(90, 45)]]}
    ]
  }, capture: {
    source: {width: 120, height: 100, sha256: pngDigest,
             url: "http://127.0.0.1/v1/references/" + id + "/source.png"},
    crop: {width: 20, height: 20, sha256: pngDigest,
           url: "http://127.0.0.1/v1/references/" + id + "/crop.png",
           pixel_integrity: {contract: "rgba8-sha256-v1", sha256: pngDigest}}
  }
});
const responseFor = (status, body, headers = {}) => {
  const bytes = body instanceof Uint8Array ? body : new TextEncoder().encode(body);
  return {status, ok: status >= 200 && status < 300,
    headers: {get: name => {
      const key = String(name).toLowerCase();
      return Object.entries(headers).find(([k]) => k.toLowerCase() === key)?.[1] || null;
    }}, body: {getReader() {
      let done = false;
      return {read: async () => done ? {done: true} :
        (done = true, {done: false, value: bytes}), cancel: async () => {done = true;}};
    }}};
};
async function run(metadata, mutate) {
  const operations = [];
  const requests = [];
  let toBlobCalls = 0;
  let resolveTerminal;
  let terminalState = "pending";
  const terminal = new Promise(resolve => { resolveTerminal = resolve; });
  const finish = state => {
    if (terminalState !== "pending") return;
    terminalState = state.kind;
    resolveTerminal(state);
  };
  let statusText = "";
  const status = {dataset: {}};
  Object.defineProperty(status, "textContent", {
    get: () => statusText,
    set: value => {
      statusText = String(value);
      if (statusText.startsWith("Could not resolve") ||
          statusText === "Invalid local reference link." ||
          statusText.startsWith("This browser cannot"))
        finish({kind: "failure", status: statusText});
    }
  });
  const save = {hidden: true, textContent: "", addEventListener: () => {}};
  Object.defineProperty(save, "href", {
    set: value => {
      save._href = value;
      finish({kind: "success", href: value});
    },
    get: () => save._href
  });
  const context = {
    drawImage: () => operations.push(["drawImage"]),
    strokeRect: (...args) => operations.push(["strokeRect", ...args]),
    beginPath: () => operations.push(["beginPath"]),
    moveTo: (...args) => operations.push(["moveTo", ...args]),
    lineTo: (...args) => operations.push(["lineTo", ...args]),
    stroke: () => operations.push(["stroke"])
  };
  const elements = {
    status, identity: {textContent: ""},
    image: {hidden: true, width: 0, height: 0,
      getContext: () => context,
      toBlob: callback => {
        ++toBlobCalls;
        callback(new Blob([png], {type: "image/png"}));
      }},
    save,
    "crop-save": {hidden: true, textContent: "", addEventListener: () => {}}
  };
  globalThis.document = {
    getElementById: id => elements[id],
    createElement: () => ({width: 0, height: 0, getContext: () => context})
  };
  globalThis.location = {origin: "http://127.0.0.1",
                          pathname: "/v1/references/" + id + "/view",
                          hash: "#cap=" + capability};
  globalThis.history = {replaceState: () => {}};
  globalThis.sessionStorage = {getItem: () => null, setItem: () => {}, removeItem: () => {}};
  globalThis.addEventListener = () => {};
  globalThis.URL.createObjectURL = () => "blob:synthetic";
  globalThis.URL.revokeObjectURL = () => {};
  globalThis.createImageBitmap = async () => ({width: 120, height: 100, close: () => {}});
  globalThis.fetch = async (url, options) => {
    const parsed = new URL(url);
    requests.push({path: parsed.pathname, hash: parsed.hash,
                   authorization: options.headers.Authorization});
    if (parsed.pathname === "/v1/references/" + id)
      return responseFor(200, JSON.stringify(mutate ? mutate(metadata) : metadata),
                         {"content-type": "application/json"});
    if (parsed.pathname === "/v1/references/" + id + "/source.png")
      return responseFor(200, png, {"content-type": "image/png",
        "x-seethis-png-sha256": pngDigest, etag: "\"sha256-" + pngDigest + "\""});
    throw new Error("unexpected request " + parsed.pathname);
  };
  vm.runInThisContext(viewer, {filename: "embedded-reference-view.js"});
  let timeoutId;
  const timeout = new Promise((_, reject) => { timeoutId = setTimeout(() =>
    reject(new Error("embedded viewer did not reach a terminal mocked state within 1000ms")), 1000); });
  const terminalResult = await Promise.race([terminal, timeout]);
  clearTimeout(timeoutId);
  return {operations, requests, status: elements.status.textContent,
          imageHidden: elements.image.hidden, saveText: elements.save.textContent,
          toBlobCalls, terminal: terminalResult};
}
const result = await run(baseMetadata());
if (result.imageHidden || !result.status.includes("freehand annotations are shown") ||
    result.saveText !== "Save annotated full-context PNG" || result.toBlobCalls !== 1 ||
    result.terminal.kind !== "success" || result.terminal.href !== "blob:synthetic")
  throw new Error("valid render did not complete: " + JSON.stringify(result));
const expectedOperations = [
  ["drawImage"],
  ["beginPath"], ["moveTo", 10, 10], ["lineTo", 20, 20],
  ["lineTo", 25, 22], ["stroke"],
  ["beginPath"], ["moveTo", 30, 70], ["lineTo", 35, 75], ["stroke"],
  ["beginPath"], ["moveTo", 80, 40], ["lineTo", 90, 45], ["stroke"]
];
if (JSON.stringify(result.operations) !== JSON.stringify(expectedOperations))
  throw new Error("unexpected freehand operations: " + JSON.stringify(result.operations));
const strokes = result.operations.filter(operation => operation[0] === "stroke").length;
const rectangles = result.operations.filter(operation => operation[0] === "strokeRect").length;
const begins = result.operations.filter(operation => operation[0] === "beginPath").length;
const moves = result.operations.filter(operation => operation[0] === "moveTo").length;
if (result.requests.length !== 2 || result.requests.some(request =>
    request.hash || request.authorization !== "Bearer " + capability))
  throw new Error("viewer request compatibility changed: " + JSON.stringify(result.requests));
const invalidCrop = await run(baseMetadata(), metadata => {
  const changed = structuredClone(metadata);
  changed.selection.regions[1].crop_pixels.x = 110;
  return changed;
});
if (!invalidCrop.status.includes("selected-region geometry invalid") ||
    !invalidCrop.imageHidden || invalidCrop.terminal.kind !== "failure")
  throw new Error("invalid crop geometry was accepted: " + JSON.stringify(invalidCrop));
const invalidPath = await run(baseMetadata(), metadata => {
  const changed = structuredClone(metadata);
  changed.selection.regions[0].subpaths[0][1].x = 121;
  return changed;
});
if (!invalidPath.status.includes("selected-region path out of bounds") ||
    invalidPath.operations.some(operation => operation[0] === "stroke") ||
    invalidPath.terminal.kind !== "failure")
  throw new Error("invalid path geometry was accepted: " + JSON.stringify(invalidPath));
console.log(JSON.stringify({strokes, rectangles, begins, moves,
  requests: result.requests.length, invalidCrop: invalidCrop.status,
  invalidPath: invalidPath.status}));
})().catch(error => {
  console.error(error);
  process.exitCode = 1;
});
)JS";
  }
  const std::string command = "/usr/bin/env node '" +
      harness_path.string() + "' '" + viewer_path.string() + "'";
  const int result = std::system(command.c_str());
  Check(result != -1 && WIFEXITED(result) && WEXITSTATUS(result) == 0,
        "embedded viewer JavaScript canvas harness passes");
}

std::string Get(std::string_view target, std::string_view token = {}) {
  std::string request = "GET " + std::string(target) + " HTTP/1.1\r\nHost: 127.0.0.1\r\n";
  if (!token.empty()) request += "Authorization: Bearer " + std::string(token) + "\r\n";
  return request + "\r\n";
}

std::string ReferenceGet(std::uint16_t port, std::string_view target,
                         std::string_view token = {},
                         std::optional<std::string_view> origin = std::nullopt,
                         std::optional<std::string_view> host = std::nullopt) {
  std::string request = "GET " + std::string(target) +
      " HTTP/1.1\r\nHost: " +
      std::string(host.value_or(std::string_view{}).empty()
                      ? "127.0.0.1:" + std::to_string(port)
                      : std::string(*host)) +
      "\r\n";
  if (origin) request += "Origin: " + std::string(*origin) + "\r\n";
  if (!token.empty())
    request += "Authorization: Bearer " + std::string(token) + "\r\n";
  return request + "\r\n";
}

struct ParsedHandoff {
  std::string target;
  std::string capability;
};

ParsedHandoff ParseCompactHandoff(std::string_view text,
                                  std::string_view base_url) {
  if (text.empty() || text.find_first_of(" \t\r\n") != std::string_view::npos)
    throw std::runtime_error("compact handoff is not one URL");
  const std::string_view url = text;
  const std::string prefix = std::string(base_url) + "/v1/references/";
  if (!url.starts_with(prefix)) throw std::runtime_error("invalid local URL");
  const auto fragment = url.find("#cap=");
  if (fragment == std::string_view::npos)
    throw std::runtime_error("missing capability fragment");
  ParsedHandoff result{std::string(url.substr(base_url.size(),
                                              fragment - base_url.size())),
                       std::string(url.substr(fragment + 5))};
  const std::string_view remainder(result.target.data() +
      std::string_view("/v1/references/").size(),
      result.target.size() - std::string_view("/v1/references/").size());
  const auto lower_hex = [](unsigned char byte) {
    return (byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f');
  };
  if (remainder.size() != 37 || !remainder.ends_with("/view") ||
      !std::all_of(remainder.begin(), remainder.begin() + 32, lower_hex) ||
      result.capability.size() != 64 ||
      !std::all_of(result.capability.begin(), result.capability.end(), lower_hex))
    throw std::runtime_error("invalid compact handoff identity");
  return result;
}

std::string PutSettings(std::string_view token, std::string_view body,
                        std::string_view origin,
                        std::string_view type = "application/json") {
  return "PUT /v1/settings HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " +
         std::string(token) + "\r\nOrigin: " + std::string(origin) +
         "\r\nContent-Type: " + std::string(type) + "\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

std::string PostRetry(std::string_view token, std::string_view body,
                      std::string_view origin,
                      std::string_view type = "application/json") {
  return "POST /v1/readiness/retry HTTP/1.1\r\nHost: 127.0.0.1\r\nAuthorization: Bearer " +
         std::string(token) + "\r\nOrigin: " + std::string(origin) +
         "\r\nContent-Type: " + std::string(type) + "\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

void TestUrlOnlyControllerCompletion() {
  TempDirectory temp;
  ReferenceStore store(temp.path / "references");
  std::mutex dispatch_mutex;
  std::vector<std::function<void()>> dispatched;
  std::vector<std::string> published;
  std::int64_t pasteboard_change_count = 10;
  int completion_observations = 0;
  MarkController marks(
      store,
      [&](std::string_view id) -> std::optional<std::int64_t> {
        published.push_back("http://127.0.0.1:1/v1/references/" +
                            std::string(id) + "/view#cap=" +
                            std::string(64, 'c'));
        return ++pasteboard_change_count;
      },
      [&](std::shared_ptr<const StoredReference>, std::int64_t) {
        ++completion_observations;
        return true;
      },
      [&](std::function<void()> task) {
        std::lock_guard lock(dispatch_mutex);
        dispatched.push_back(std::move(task));
      });
  const auto run_dispatched = [&] {
    std::vector<std::function<void()>> ready;
    {
      std::lock_guard lock(dispatch_mutex);
      ready.swap(dispatched);
    }
    for (auto& task : ready) task();
  };
  const auto ready_id = std::string(32, '6');
  const auto observed = Now();
  const auto ready_context = ContextFixture(observed);
  Observe(marks, ready_context);
  auto generation = marks.Begin(ready_id);
  Check(marks.BindContext(generation, ready_context),
        "URL-only ready fixture binds context");
  marks.Release(generation,
                {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1},
                 {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}},
                {observed.utc_us + 30, observed.monotonic_us + 30});
  Check(published.size() == 1 &&
            marks.copy_result() == CopyResult::kCopied,
        "capture release immediately publishes one pending URL");
  const auto stable_url = published.back();
  Observe(marks, ready_context);
  Check(marks.RecopyJob(ready_id) && published.back() == stable_url,
        "deliberate pending click republishes the stable URL");
  pasteboard_change_count = 5'000;
  CaptureResult capture;
  capture.requested = {observed.utc_us + 10, observed.monotonic_us + 10};
  capture.completed = {observed.utc_us + 20, observed.monotonic_us + 20};
  capture.pixels = {100, 100, Bytes(100 * 100 * 4)};
  for (std::size_t index = 3; index < capture.pixels.rgba.size(); index += 4)
    capture.pixels.rgba[index] = 255;
  marks.Captured(generation, std::move(capture));
  store.WaitForIdleForTesting();
  run_dispatched();
  Check(completion_observations == 1 && pasteboard_change_count == 5'000 &&
            store.LookupMetadata(ready_id)->state == ReferenceJobState::kReady &&
            marks.copy_result() == CopyResult::kCopied &&
            marks.status() == "Reference ready; click its mark to copy the URL",
        "unowned ready completion preserves the original result without claiming URL presence");
  std::cout<<"uc2_unrelated_ready,count="<<pasteboard_change_count
           <<",copy_result="<<static_cast<int>(marks.copy_result())
           <<",status="<<marks.status()<<'\n';
  Observe(marks, ready_context);
  Check(marks.RecopyJob(ready_id) && published.back() == stable_url &&
            pasteboard_change_count == 5'001,
        "deliberate ready click alone republishes one stable URL");
  struct Attempt {
    bool success = false;
    long long elapsed_us = 0;
  };
  std::vector<Attempt> copy_attempts;
  Observe(marks, ready_context);
  for (int attempt = 0; attempt < 220; ++attempt) {
    const auto start = std::chrono::steady_clock::now();
    const bool success = marks.RecopyJob(ready_id);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    if (attempt >= 20) copy_attempts.push_back({success, elapsed});
  }
  std::vector<long long> sorted_copy;
  bool every_copy_succeeded = true;
  for (std::size_t index = 0; index < copy_attempts.size(); ++index) {
    const auto& attempt = copy_attempts[index];
    std::cout << "deterministic_url_copy_attempt," << index
              << ",status=" << (attempt.success ? "published" : "failed")
              << ",elapsed_us=" << attempt.elapsed_us << '\n';
    every_copy_succeeded = every_copy_succeeded && attempt.success;
    sorted_copy.push_back(attempt.elapsed_us);
  }
  std::sort(sorted_copy.begin(), sorted_copy.end());
  std::cout << "deterministic_url_copy_p95_us=" << sorted_copy[189] << '\n';
  Check(copy_attempts.size() == 200 && every_copy_succeeded,
        "20 warmups and 200 ordered deterministic URL-copy attempts succeed");

  std::vector<Attempt> hover_attempts;
  const auto topology = ContextFixture(observed).displays;
  const DisplayPoint hover_point{
      1, CoordinateUnit::kLogicalPoints, {20, 20}, 1};
  Observe(marks, ready_context);
  for (int attempt = 0; attempt < 220; ++attempt) {
    const auto start = std::chrono::steady_clock::now();
    const auto hit = marks.HitIdentity(hover_point, 2, topology);
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - start).count();
    const bool success = hit && *hit == ready_id;
    if (attempt >= 20) hover_attempts.push_back({success, elapsed});
  }
  std::vector<long long> sorted_hover;
  bool every_hover_succeeded = true;
  for (std::size_t index = 0; index < hover_attempts.size(); ++index) {
    const auto& attempt = hover_attempts[index];
    std::cout << "deterministic_hover_attempt," << index
              << ",status=" << (attempt.success ? "hit" : "miss")
              << ",elapsed_us=" << attempt.elapsed_us << '\n';
    every_hover_succeeded = every_hover_succeeded && attempt.success;
    sorted_hover.push_back(attempt.elapsed_us);
  }
  std::sort(sorted_hover.begin(), sorted_hover.end());
  std::cout << "deterministic_hover_p95_us=" << sorted_hover[189] << '\n';
  Check(hover_attempts.size() == 200 && every_hover_succeeded,
        "20 warmups and 200 ordered deterministic hover attempts stay separate");

  const auto failed_id = std::string(32, '7');
  generation = marks.Begin(failed_id);
  Check(marks.BindContext(generation, ContextFixture(Now())),
        "URL-only failed fixture binds context");
  marks.Release(generation,
                {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1}},
                Now());
  const auto failed_url = published.back();
  pasteboard_change_count = 7'000;
  CaptureResult failed;
  failed.error = "capture_failed";
  marks.Captured(generation, std::move(failed));
  store.WaitForIdleForTesting();
  run_dispatched();
  Check(completion_observations == 1 && pasteboard_change_count == 7'000 &&
            store.LookupMetadata(failed_id)->state == ReferenceJobState::kFailed &&
            marks.copy_result() == CopyResult::kFailed &&
            marks.status() == "Reference failed: capture_failed",
        "failed completion leaves unrelated clipboard content unchanged and reports failure");
  std::cout<<"uc2_failed,count="<<pasteboard_change_count
           <<",copy_result="<<static_cast<int>(marks.copy_result())
           <<",status="<<marks.status()<<'\n';
  Check(marks.RecopyJob(failed_id) && published.back() == failed_url &&
            pasteboard_change_count == 7'001,
        "deliberate failed-mark click republishes the stable status URL");

  const auto unchanged_id = std::string(32, '8');
  const auto unchanged_observed = Now();
  generation = marks.Begin(unchanged_id);
  Check(marks.BindContext(generation, ContextFixture(unchanged_observed)),
        "unchanged-count ready fixture binds context");
  marks.Release(generation,
                {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1},
                 {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}},
                {unchanged_observed.utc_us + 30,
                 unchanged_observed.monotonic_us + 30});
  const auto unchanged_publication_count = pasteboard_change_count;
  CaptureResult unchanged_capture;
  unchanged_capture.requested = {unchanged_observed.utc_us + 10,
                                 unchanged_observed.monotonic_us + 10};
  unchanged_capture.completed = {unchanged_observed.utc_us + 20,
                                 unchanged_observed.monotonic_us + 20};
  unchanged_capture.pixels = {100, 100, Bytes(100 * 100 * 4)};
  for(std::size_t index=3;index<unchanged_capture.pixels.rgba.size();index+=4)
    unchanged_capture.pixels.rgba[index]=255;
  marks.Captured(generation,std::move(unchanged_capture));
  store.WaitForIdleForTesting();
  run_dispatched();
  Check(completion_observations==2&&
            pasteboard_change_count==unchanged_publication_count&&
            marks.copy_result()==CopyResult::kCopied&&
            marks.status()=="Reference ready; click its mark to copy the URL",
        "unchanged-count completion is lifecycle-ready without claiming current ownership");
  std::cout<<"uc2_unchanged_ready,count="<<pasteboard_change_count
           <<",copy_result="<<static_cast<int>(marks.copy_result())
           <<",status="<<marks.status()<<'\n';
}

void TestChromeTransactionIdentityFreeze() {
  TempDirectory temp;
  ReferenceStore store(temp.path / "chrome-transaction-freeze");
  int copied = 0;
  MarkController marks(store, [&](const auto&) { ++copied; return true; });
  const auto id = std::string(32, '5');
  const auto observed = Now();
  auto context = ContextFixture(observed);
  context.bundle_id = "com.google.Chrome";
  context.window_title = "redacted capture title";
  Observe(marks, context);

  const auto generation = marks.Begin(id);
  Check(marks.BindContext(generation, context),
        "Chrome transaction binds its original window");
  PageIdentity page_a;
  page_a.bundle_id = context.bundle_id;
  page_a.browser_pid = context.window_pid;
  page_a.window_id = context.window_id;
  page_a.window_bounds = context.window;
  page_a.process_start_identity_us = 700;
  page_a.opaque_tab_id = "page-a";
  page_a.navigation_digest =
      seethis::platform::ChromeNavigationDigest("https://example.test/a");
  Check(marks.BindPageIdentity(generation, page_a),
        "Chrome transaction freezes page A while drawing");
  marks.SetPageObservation(
      {PageAvailability::kReady, 1, Now(), page_a});

  auto page_b = page_a;
  page_b.opaque_tab_id = "page-b";
  page_b.navigation_digest =
      seethis::platform::ChromeNavigationDigest("https://example.test/b");
  marks.SetPageObservation(
      {PageAvailability::kReady, 2, Now(), page_b});

  CaptureResult capture;
  capture.requested = {observed.utc_us + 10, observed.monotonic_us + 10};
  capture.completed = {observed.utc_us + 20, observed.monotonic_us + 20};
  capture.pixels = {100, 100, Bytes(100 * 100 * 4)};
  for (std::size_t index = 3; index < capture.pixels.rgba.size(); index += 4)
    capture.pixels.rgba[index] = 255;
  marks.Captured(generation, std::move(capture));
  marks.Release(generation,
                {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1},
                 {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}},
                {observed.utc_us + 30, observed.monotonic_us + 30});
  store.WaitForIdleForTesting();

  const auto stored = store.Lookup(id);
  Check(stored && stored->ready && stored->ready->value.page_identity &&
            SamePageIdentity(*stored->ready->value.page_identity, page_a) &&
            !SamePageIdentity(*stored->ready->value.page_identity, page_b),
        "release persists the page identity frozen during drawing");
  Check(marks.marks().empty() &&
            !marks.HitIdentity({1, CoordinateUnit::kLogicalPoints,
                                {20, 20}, 1},
                               2, context.displays) &&
            !marks.RecopyJob(id),
        "same-window page B hides page A after persistence");

  marks.SetPageObservation(
      {PageAvailability::kReady, 3, Now(), page_a});
  Check(marks.marks().size() == 1 &&
            marks.HitIdentity({1, CoordinateUnit::kLogicalPoints,
                               {20, 20}, 1},
                              2, context.displays) == id &&
            marks.RecopyJob(id) && copied == 2,
        "returning to page A restores its stored mark and copy target");
}

void TestAnnotatedInterleaving() {
  TempDirectory temp;
  const auto reference_root=temp.path/"interleaving";
  const auto web=temp.path/"web";
  std::filesystem::create_directories(web);
  std::ofstream(web/"index.html")<<"<html>synthetic</html>";
  SettingsStore settings(temp.path/"settings.json");
  const std::string a(32,'a'),b(32,'b');
  const auto token_a=settings.ReferenceCapability(a);
  const auto token_b=settings.ReferenceCapability(b);
  auto run=[&](ReferenceStore& references,bool reopened) {
    auto readiness=std::make_shared<seethis::platform::PermissionReadinessState>();
    seethis::service::ReferenceServer server(references,settings,web,readiness);
    std::string error;
    Check(server.Start(&error),"interleaving HTTP server starts");
    auto get=[&](const std::string& id,const std::string& suffix,const std::string& token) {
      return Request(server.port(),ReferenceGet(server.port(),"/v1/references/"+id+suffix,token));
    };
    const auto source=get(a,"/source.png",token_a);
    (void)get(a,"/crop.png",token_a);references.WaitForIdleForTesting();
    const auto crop=get(a,"/crop.png",token_a);
    Check(source.status==200&&crop.status==200,"interleaving preserves source/crop HTTP delivery");
    const auto source_hash=Sha256(Bytes(source.body.begin(),source.body.end()));
    const auto crop_hash=Sha256(Bytes(crop.body.begin(),crop.body.end()));
    Check(Header(source,"X-SeeThis-PNG-SHA256")==source_hash&&
          Header(crop,"X-SeeThis-PNG-SHA256")==crop_hash,
          "source and crop byte digests remain truthful");
    (void)get(a,"",token_a);references.WaitForIdleForTesting();
    const auto marked=get(a,"/annotated.png",token_a);
    const auto bytes=Bytes(marked.body.begin(),marked.body.end());
    const auto digest=Sha256(bytes);
    Check(marked.status==200&&Header(marked,"Content-Type")=="image/png"&&
          Header(marked,"ETag")=="\"sha256-"+digest+"\""&&
          Header(marked,"X-SeeThis-PNG-SHA256")==digest&&
          FileBytes(reference_root/(a+".annotated.png"))==bytes,
          "marked HTTP header and persisted PNG agree");
    for(int round=0;round<4;++round) {
      Check(get(b,"",token_b).status==200,"other-reference metadata remains available");
      references.WaitForIdleForTesting();
      const auto interleaved=get(a,"/annotated.png",token_a);
      Check(interleaved.status==200&&interleaved.body==marked.body&&
            Header(interleaved,"X-SeeThis-PNG-SHA256")==digest,
            "other-reference metadata must not turn a completed marked PNG back into202");
    }
    std::vector<Response> concurrent(4);
    std::vector<std::thread> threads;
    for(int index=0;index<4;++index)
      threads.emplace_back([&,index]{concurrent[index]=get(a,"/annotated.png",token_a);});
    for(auto& thread:threads)thread.join();
    for(const auto& response:concurrent)
      Check(response.status==200&&response.body==marked.body,"concurrent marked readers retain exact bytes");
    Check(get(a,"/annotated.png","").status==401&&
          get(a,"/annotated.png",token_b).status==403,
          "cached marked images still enforce reference authentication");
    Check(get(a,"/source.png",token_a).body==source.body&&
          get(a,"/crop.png",token_a).body==crop.body,
          "marked cache does not alter source or crop");
    if(reopened) {
      for(std::size_t index=0;index<ReferenceStore::kMaximumCachedReadyPayloads;++index) {
        const std::string id(32,static_cast<char>('0'+index));
        MakeReference(references,id,0,false,true);
        const auto token=settings.ReferenceCapability(id);
        (void)get(id,"/annotated.png",token);references.WaitForIdleForTesting();
        Check(get(id,"/annotated.png",token).status==200,"bounded cache turnover completes each cold reference");
      }
      Check(get(a,"/annotated.png",token_a).status==202,"old marked payload is evicted at the bounded cache limit");
      references.WaitForIdleForTesting();
      Check(get(a,"/annotated.png",token_a).body==marked.body,"evicted PNG reloads exact persisted bytes");
      Check(references.Delete(a)==DeleteResult::kDeleted&&
            get(a,"/annotated.png",token_a).status==410&&
            !std::filesystem::exists(reference_root/(a+".annotated.png")),
            "deletion drops cached marked image and preserves410");
    }
    server.Stop();references.WaitForIdleForTesting();
  };
  {
    ReferenceStore references(reference_root);
    MakeReference(references,a,0,false,true);MakeReference(references,b,0,false,true);
    run(references,false);
  }
  ReferenceStore reopened(reference_root);
  run(reopened,true);
}

}  // namespace

int main() {
  try {
    TestUrlOnlyControllerCompletion();
    TestChromeTransactionIdentityFreeze();
    TestAnnotatedInterleaving();
    TempDirectory temp;
    const auto legacy_path = temp.path / "legacy-settings.json";
    const std::string legacy_secret(64, '1');
    {
      std::ofstream legacy(legacy_path);
      legacy << "{\"schema\":1,\"shortcut_key_code\":12,"
                "\"shortcut_modifiers\":9,\"maximum_hold_ms\":30000,"
                "\"crop_margin_points\":8,\"mark_hit_radius_points\":10,"
                "\"reference_ttl_seconds\":604800,"
                "\"maximum_visible_references\":500,"
                "\"reference_secret\":\"" << legacy_secret << "\"}\n";
    }
    SettingsStore migrated_settings(legacy_path);
    const auto migrated_bytes = FileBytes(legacy_path);
    const std::string migrated_text(migrated_bytes.begin(),migrated_bytes.end());
    const std::string migration_id(32,'8');
    const std::string expected_material =
        legacy_secret + ":reference-read:" + migration_id;
    Check(migrated_settings.Get().shortcut_key_code == 12 &&
              migrated_settings.Get().shortcut_modifiers == 9 &&
              migrated_settings.Get().delete_shortcut_key_code == 2 &&
              migrated_settings.Get().delete_shortcut_modifiers ==
                  kShortcutOption &&
              migrated_settings.ReferenceCapability(migration_id) ==
                  Sha256(Bytes(expected_material.begin(),expected_material.end())) &&
              migrated_text.find("\"schema\":2") != std::string::npos &&
              migrated_text.find("\"delete_shortcut_key_code\":2") !=
                  std::string::npos,
          "legacy settings migrate capture and secret while adding Option+D");
    const auto settings_path = temp.path / "settings.json";
    SettingsStore settings(settings_path);
    Check(std::filesystem::exists(settings_path), "default settings persist");
    struct stat info{};
    Check(stat(settings_path.c_str(), &info) == 0 && (info.st_mode & 0777) == 0600,
          "settings file is owner-only");
    const auto first_capability = settings.ReferenceCapability(std::string(32, 'a'));
    SettingsStore reloaded(settings_path);
    Check(reloaded.ReferenceCapability(std::string(32, 'a')) == first_capability,
          "reference capability remains stable across restart");
    Check(SettingsJson(settings.Get()).find("secret") == std::string::npos,
          "public settings omit capability root");
    Check(settings.Get().shortcut_key_code == 0 &&
              settings.Get().shortcut_modifiers == kShortcutOption &&
              settings.Get().delete_shortcut_key_code == 2 &&
              settings.Get().delete_shortcut_modifiers == kShortcutOption,
          "new settings keep Option+A capture and use distinct Option+D deletion");
    std::string parse_error;
    Check(!ParseSettingsJson("{}", &parse_error), "missing settings rejected");
    Check(!ParseSettingsJson("{\"schema\":1 garbage}", &parse_error),
          "malformed JSON syntax is rejected");
    const std::string public_legacy =
        "{\"schema\":1,\"shortcut_key_code\":0,\"shortcut_modifiers\":4,"
        "\"maximum_hold_ms\":30000,\"crop_margin_points\":8,"
        "\"mark_hit_radius_points\":10,\"reference_ttl_seconds\":604800,"
        "\"maximum_visible_references\":500}";
    Check(!ParseSettingsJson(public_legacy,&parse_error),
          "public schema-1 update cannot erase the separate delete binding");
    auto invalid = settings.Get();
    invalid.shortcut_modifiers = 0;
    Check(!settings.Update(invalid, &parse_error), "invalid native settings rejected");
    auto conflict = settings.Get();
    conflict.delete_shortcut_key_code = conflict.shortcut_key_code;
    conflict.delete_shortcut_modifiers = conflict.shortcut_modifiers;
    Check(!settings.Update(conflict,&parse_error)&&
              settings.Get().delete_shortcut_key_code == 2,
          "capture/delete binding conflicts are rejected without mutation");

    const auto reference_root = temp.path / "references";
    const std::string current_id(32, 'a');
    const std::string old_id(32, 'b');
    const std::string deleted_id(32, '9');
    const std::string multi_id(32, '8');
    const std::string queued_multi_id(32, '7');
    StoredReference current, old, deleted, multi, queued_multi;
    {
      ReferenceStore creator(reference_root);
      current = MakeReference(creator, current_id, 0, true);
      old = MakeReference(creator, old_id, 120);
      deleted = MakeReference(creator, deleted_id, 0);
      multi = MakeReference(creator, multi_id, 0, false, true);
      queued_multi=MakeReference(creator,queued_multi_id,0,false,true);
    }
    Check(std::filesystem::exists(reference_root/(multi_id+".source.png"))&&
              std::filesystem::exists(reference_root/(multi_id+".ready"))&&
              !std::filesystem::exists(reference_root/(multi_id+".stref"))&&
              !std::filesystem::exists(reference_root/(multi_id+".crop.png")),
          "schema-2 service fixture stores one primary image and small metadata");
    ReferenceStore references(reference_root);
    const auto cold = references.LookupMetadata(current_id);
    const auto cold_record_size =
        std::filesystem::file_size(reference_root / (current_id + ".stref"));
    Check(cold && cold->state == ReferenceJobState::kReady && cold->metadata &&
              !cold->ready && cold->metadata->source.bytes.empty() &&
              cold->metadata->crop.bytes.empty() &&
              cold_record_size >= 59'000'000,
          "cold restart exposes lightweight metadata for a representative 59 MB record");
    std::cout << "cold_record_bytes=" << cold_record_size << '\n';
    const auto web = temp.path / "web";
    std::filesystem::create_directories(web);
    { std::ofstream(web / "index.html") << "<html>settings</html>"; }
    { std::ofstream(web / "settings.js") << "void 0;"; }
    auto readiness =
        std::make_shared<seethis::platform::PermissionReadinessState>();
    readiness->input_monitor.store(
        seethis::platform::InputMonitorState::kDenied);
    readiness->screen_recording.store(
        seethis::platform::ScreenRecordingState::kReady);
    readiness->delete_shortcut.store(
        seethis::platform::DeleteShortcutState::kInputUnavailable);
    readiness->app_name = "SeeThis Test";
    readiness->bundle_identifier = "local.seethis.overlay";
    readiness->bundle_path = "/Applications/SeeThis Test.app";
    seethis::service::ReferenceServer server(references, settings, web,
                                              readiness);
    std::string start_error;
    Check(server.Start(&start_error) && server.port() != 0, "server binds an ephemeral loopback port");
    const auto settings_token = server.settings_capability_for_testing();
    Check(settings_token.size() == 64, "settings capability has 256-bit material");
    std::vector<std::string> historical_publications;
    std::int64_t historical_change_count = 20;
    int historical_completion_publications = 0;
    MarkController historical_copy(
        references,
        [&](std::string_view id) -> std::optional<std::int64_t> {
          historical_publications.push_back(server.ClipboardText(id));
          return ++historical_change_count;
        },
        [&](std::shared_ptr<const StoredReference>, std::int64_t) {
          ++historical_completion_publications;
          return true;
        },
        [](std::function<void()> task) { task(); });
    Observe(historical_copy, current.value.context);
    struct HistoricalRecopyAttempt {
      bool copied = false;
      bool metadata_only = false;
      bool stable_url = false;
      long long elapsed_us = 0;
    };
    const auto expected_historical_url = server.ClipboardText(current_id);
    std::vector<HistoricalRecopyAttempt> historical_recopy_attempts;
    for(int attempt=0;attempt<220;++attempt) {
      const auto start=std::chrono::steady_clock::now();
      const bool copied=historical_copy.RecopyJob(current_id);
      const auto metadata=references.LookupMetadata(current_id);
      const bool metadata_only=metadata&&metadata->state==ReferenceJobState::kReady&&
                               !metadata->ready;
      const bool stable_url=!historical_publications.empty()&&
                            historical_publications.back()==expected_historical_url;
      const auto elapsed=std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now()-start).count();
      if(attempt>=20)historical_recopy_attempts.push_back(
          {copied,metadata_only,stable_url,elapsed});
    }
    std::vector<long long> sorted_historical_recopy;
    bool every_historical_recopy_succeeded=true;
    for(std::size_t index=0;index<historical_recopy_attempts.size();++index) {
      const auto& attempt=historical_recopy_attempts[index];
      const bool success=attempt.copied&&attempt.metadata_only&&attempt.stable_url;
      std::cout<<"large_historical_recopy_attempt,"<<index<<",status="
               <<(success?"published_metadata_only":"failed_or_materialized")
               <<",elapsed_us="<<attempt.elapsed_us<<'\n';
      every_historical_recopy_succeeded=
          every_historical_recopy_succeeded&&success;
      sorted_historical_recopy.push_back(attempt.elapsed_us);
    }
    std::sort(sorted_historical_recopy.begin(),sorted_historical_recopy.end());
    std::cout<<"large_historical_recopy_p95_us="
             <<sorted_historical_recopy[189]<<'\n';
    Check(cold_record_size>=27ULL*1024*1024&&
              historical_recopy_attempts.size()==200&&
              every_historical_recopy_succeeded&&
              historical_publications.size()==220&&
              historical_completion_publications==0,
          "20 warmups and 200 ordered 27 MiB historical recopies remain metadata-only");

    auto response = Request(server.port(), Get("/settings/"));
    Check(response.status == 200 && response.body.find("settings") != std::string::npos,
          "settings HTML is served without embedding capability");
    Check(response.bytes.find(settings_token) == std::string::npos,
          "settings capability is absent from HTTP response");
    Check(Request(server.port(), Get("/v1/settings")).status == 401,
          "missing settings bearer is explicit");
    Check(Request(server.port(), Get("/v1/settings", "wrong")).status == 403,
          "wrong settings bearer is forbidden");
    Check(Request(server.port(), Get("/v1/settings", settings_token)).status == 200,
          "scoped settings bearer reads settings");
    Check(Request(server.port(), Get("/v1/readiness")).status == 401,
          "readiness requires the settings bearer");
    Check(Request(server.port(), Get("/v1/readiness", "wrong")).status == 403,
          "readiness rejects the wrong settings bearer");
    response = Request(server.port(), Get("/v1/readiness", settings_token));
    Check(response.status == 200 &&
              response.body.find("\"state\":\"denied\",\"ready\":false") !=
                  std::string::npos &&
              response.body.find("\"state\":\"ready\",\"ready\":true") !=
                  std::string::npos,
          "readiness keeps input and screen authorization separate");
    Check(response.body.find(
              "\"delete_shortcut\":{\"state\":\"input_unavailable\",\"ready\":false") !=
              std::string::npos,
          "readiness reports delete observation independently");
    Check(response.body.find("local.seethis.overlay") != std::string::npos &&
              response.body.find("/Applications/SeeThis Test.app") !=
                  std::string::npos &&
              response.body.find("cdhash-bound grant") != std::string::npos,
          "readiness reports exact app identity, location and update policy");
    Check(response.body.find("Enable Input Monitoring for this exact app") !=
                  std::string::npos,
          "readiness exposes Input Monitoring recovery guidance");
    Check(response.body.find(settings_token) == std::string::npos,
          "readiness response does not disclose its capability");
    readiness->screen_recording.store(
        seethis::platform::ScreenRecordingState::kDenied);
    response = Request(server.port(), Get("/v1/readiness", settings_token));
    Check(response.status == 200 &&
              response.body.find("\"screen_recording\":{\"state\":\"denied\",\"ready\":false") !=
                  std::string::npos,
          "service reports denied Screen Recording independently");
    Check(response.body.find("Enable Screen Recording for this exact app") !=
                  std::string::npos,
          "readiness exposes Screen Recording recovery guidance independently");
    readiness->screen_recording.store(
        seethis::platform::ScreenRecordingState::kReady);
    const std::pair<seethis::platform::InputMonitorState, std::string_view>
        reported_states[] = {
            {seethis::platform::InputMonitorState::kTapCreationFailed,
             "tap_creation_failed"},
            {seethis::platform::InputMonitorState::kPartialTap, "partial_tap"},
            {seethis::platform::InputMonitorState::kDisabled, "disabled"},
            {seethis::platform::InputMonitorState::kRestartRequired,
             "restart_required"},
            {seethis::platform::InputMonitorState::kGranted, "granted"},
            {seethis::platform::InputMonitorState::kRecovered, "recovered"},
        };
    for (const auto& [state, name] : reported_states) {
      readiness->input_monitor.store(state);
      response = Request(server.port(), Get("/v1/readiness", settings_token));
      const std::string expected = "\"state\":\"" + std::string(name) +
          "\",\"ready\":" +
          (seethis::platform::InputMonitorReady(state) ? "true" : "false");
      Check(response.status == 200 &&
                response.body.find(expected) != std::string::npos,
            "service reports each deterministic input state");
    }
    const std::pair<seethis::platform::DeleteShortcutState,std::string_view>
        delete_states[] = {
          {seethis::platform::DeleteShortcutState::kUnknown,"unknown"},
          {seethis::platform::DeleteShortcutState::kInputUnavailable,
           "input_unavailable"},
          {seethis::platform::DeleteShortcutState::kConflict,"conflict"},
          {seethis::platform::DeleteShortcutState::kRegistrationFailed,
           "registration_failed"},
          {seethis::platform::DeleteShortcutState::kSecureInput,"secure_input"},
          {seethis::platform::DeleteShortcutState::kReady,"ready"},
        };
    for(const auto& [state,name]:delete_states) {
      readiness->delete_shortcut.store(state);
      response=Request(server.port(),Get("/v1/readiness",settings_token));
      const std::string expected="\"delete_shortcut\":{\"state\":\""+
          std::string(name)+"\",\"ready\":"+
          (seethis::platform::DeleteShortcutReady(state)?"true":"false");
      Check(response.status==200&&response.body.find(expected)!=std::string::npos,
            "service reports each deterministic delete-shortcut state");
    }
    const auto retry_before = readiness->retry_generation.load();
    Check(Request(server.port(), PostRetry("", "{}",
          server.base_url())).status == 401,
          "input retry requires the settings bearer");
    Check(Request(server.port(), PostRetry("wrong", "{}",
          server.base_url())).status == 403,
          "input retry rejects the wrong settings bearer");
    Check(Request(server.port(), Get("/v1/readiness/retry",
          settings_token)).status == 405,
          "input retry rejects unsupported methods");
    Check(Request(server.port(), PostRetry(settings_token, "{}",
          "http://evil.invalid")).status == 403,
          "input retry rejects a foreign Origin");
    Check(Request(server.port(), PostRetry(settings_token, "{}",
          server.base_url(), "text/plain")).status == 415,
          "input retry rejects unsupported content type");
    Check(Request(server.port(), PostRetry(settings_token, "{\"extra\":1}",
          server.base_url())).status == 422,
          "input retry rejects a nonempty request schema");
    Check(Request(server.port(), PostRetry(settings_token, "{}",
          server.base_url())).status == 202 &&
              readiness->retry_generation.load() == retry_before + 1,
          "authorized input retry publishes one bounded request");
    Check(settings.Get().shortcut_key_code == reloaded.Get().shortcut_key_code &&
              settings.Get().shortcut_modifiers ==
                  reloaded.Get().shortcut_modifiers,
          "input retry does not mutate saved shortcut settings");

    auto changed = settings.Get();
    changed.shortcut_key_code = 12;
    changed.shortcut_modifiers = kShortcutCommand | kShortcutShift;
    changed.reference_ttl_seconds = 60;
    changed.maximum_visible_references = 500;
    const std::string changed_json = SettingsJson(changed);
    const std::string origin = server.base_url();
    Check(Request(server.port(), PutSettings(settings_token,
          changed_json, "http://evil.invalid")).status == 403,
          "settings mutation rejects foreign Origin");
    Check(Request(server.port(), PutSettings(settings_token,
          changed_json, origin, "text/plain")).status == 415,
          "settings mutation rejects unsupported content type");
    Check(Request(server.port(), PutSettings(settings_token,
          "{}", origin)).status == 422,
          "settings mutation rejects malformed schema");
    response = Request(server.port(), PutSettings(settings_token,
                           changed_json, origin));
    Check(response.status == 200 && settings.Get().shortcut_key_code == 12,
          "authorized valid settings mutation is applied");
    Check(response.body.find("input_monitoring") == std::string::npos,
          "settings persistence response does not fabricate readiness");
    SettingsStore updated(settings_path);
    Check(updated.Get().shortcut_key_code == 12 &&
          updated.ReferenceCapability(current_id) ==
              settings.ReferenceCapability(current_id),
          "atomic update survives reload without rotating reference scope");

    const std::string current_token = settings.ReferenceCapability(current_id);
    const std::string current_root = "/v1/references/" + current_id;
    const std::string multi_token = settings.ReferenceCapability(multi_id);
    const std::string multi_root = "/v1/references/" + multi_id;
    const std::string queued_multi_token =
        settings.ReferenceCapability(queued_multi_id);
    const std::string queued_multi_root =
        "/v1/references/" + queued_multi_id;
    const std::string unknown_id(32, 'c');
    const std::string unknown_view = "/v1/references/" + unknown_id + "/view";
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root + "/view"));
    const auto unknown_response = Request(server.port(), ReferenceGet(
        server.port(), unknown_view));
    Check(response.status == 200 && response.body == unknown_response.body &&
              response.body.find(current_id) == std::string::npos &&
              response.body.find(current_token) == std::string::npos,
          "generic viewer shell reveals neither existence, identity nor capability");
    Check(response.body.find("<h2>Current local operation</h2>") !=
                  std::string::npos &&
              response.body.find("<h2>Historical capture metadata</h2>") !=
                  std::string::npos &&
              response.body.find("Readiness and Clipboard acknowledgements are current local operation facts") !=
                  std::string::npos &&
              response.body.find("<pre id=\"identity\"") != std::string::npos,
          "viewer separates current local operation from historical metadata text");
    Check(response.bytes.find(
              "Content-Security-Policy: default-src 'none'; script-src 'self'; "
              "connect-src 'self'; img-src blob:; style-src 'none'; "
              "object-src 'none'; base-uri 'none'; frame-ancestors 'none'; "
              "form-action 'none'\r\n") != std::string::npos &&
              response.bytes.find("Cache-Control: no-store\r\n") !=
                  std::string::npos &&
              response.bytes.find("Referrer-Policy: no-referrer\r\n") !=
                  std::string::npos,
          "viewer shell has the exact restrictive response policy");
    response = Request(server.port(), ReferenceGet(
        server.port(), "/v1/reference-view.js"));
    RunEmbeddedViewerCanvasHarness(response.body);
    Check(response.status == 200 &&
              response.body.find("history.replaceState") != std::string::npos &&
              response.body.find("Authorization: `Bearer") != std::string::npos &&
              response.body.find("crypto.subtle.digest") != std::string::npos &&
              response.body.find("URL.createObjectURL") != std::string::npos &&
              response.body.find("sessionStorage") != std::string::npos &&
              response.body.find("rememberCapability") != std::string::npos &&
              response.body.find("forgetCapability") != std::string::npos &&
              response.body.find("const initialHash = location.hash") !=
                  std::string::npos &&
              response.body.find(
                  "const invalidFragment = initialHash && !fragment") !=
                  std::string::npos &&
              response.body.find(
                  "if (path && invalidFragment) forgetCapability()") !=
                  std::string::npos &&
              response.body.find("clearCredentialOnFailure") != std::string::npos &&
              response.body.find("readBounded(response, maximum)") !=
                  std::string::npos &&
              response.body.find("local fetch exceeded 2 seconds") !=
                  std::string::npos &&
              response.body.find("performance.now()") != std::string::npos &&
              response.body.find("Indexing local reference") !=
                  std::string::npos &&
              response.body.find("Waiting for reference metadata") !=
                  std::string::npos &&
              response.body.find("contextImageReadyMs") != std::string::npos &&
              response.body.find("cropReadyMs") != std::string::npos &&
              response.body.find("toBlob") != std::string::npos &&
              response.body.find("verifyCropPixels") != std::string::npos &&
              response.body.find("if (!legacyDigest) await verifyCropPixels") !=
                  std::string::npos &&
              response.body.find("cropDigest !== legacyDigest") !=
                  std::string::npos &&
              response.body.find("crop pixel integrity mismatch") !=
                  std::string::npos &&
              response.body.find("crop-save") != std::string::npos &&
              response.body.find("sourcePoint") != std::string::npos &&
              response.body.find("beginPath") != std::string::npos &&
              response.body.find("renderHistoricalMetadata") != std::string::npos &&
              response.body.find("timestampText") != std::string::npos &&
              response.body.find("Historical capture facts (captured metadata only)") !=
                  std::string::npos &&
              response.body.find("Application name:") != std::string::npos &&
              response.body.find("Window title:") != std::string::npos &&
              response.body.find("Bundle identifier:") != std::string::npos &&
              response.body.find("Capture requested (UTC microseconds):") !=
                  std::string::npos &&
              response.body.find("Circle completed (UTC microseconds):") !=
                  std::string::npos &&
              response.body.find("Pixel dimensions:") != std::string::npos &&
              response.body.find("Logical bounds (shared global AppKit logical points, Y-up)") !=
                  std::string::npos &&
              response.body.find("Region ${regionIndex + 1} (independent):") !=
                  std::string::npos &&
              response.body.find("display-local logical point") !=
                  std::string::npos &&
              response.body.find("shared global AppKit logical points are Y-up") !=
                  std::string::npos &&
              response.body.find("historical metadata unavailable on this Mac") !=
                  std::string::npos &&
              response.body.find("reference has expired on this Mac") !=
                  std::string::npos &&
              response.body.find("pending.retry_ms") != std::string::npos &&
              response.body.find("Math.max(20") != std::string::npos &&
              response.body.find("Math.min(250") != std::string::npos &&
              response.body.find("pending.state !== \"deriving\"") !=
                  std::string::npos &&
              response.body.find("attempt < 40") != std::string::npos &&
              response.body.find("reference was deleted from this Mac") !=
                  std::string::npos &&
              response.body.find("attempt < 20") != std::string::npos &&
              response.body.find(current_token) == std::string::npos,
          "fixed viewer script scrubs, authenticates, verifies and displays locally");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view", {}, std::nullopt,
              "127.0.0.1")).status == 400,
          "viewer rejects a Host without the exact bound port");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view", {},
              "http://evil.invalid")).status == 403,
          "viewer rejects an explicit foreign Origin");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view", {},
              server.base_url())).status == 200,
          "viewer permits the exact loopback Origin");
    Check(Request(server.port(), "POST " + current_root +
              "/view HTTP/1.1\r\nHost: 127.0.0.1:" +
              std::to_string(server.port()) + "\r\n\r\n").status == 405,
          "viewer rejects unsupported methods");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view?cap=x")).status == 400,
          "viewer rejects query parameters");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root + "/view#cap=x")).status == 400,
          "server rejects a fragment-like HTTP request target");

    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root)).status == 401,
          "reference read requires bearer");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root, "wrong")).status == 403,
          "reference read rejects wrong capability");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root, current_token, std::nullopt,
              "localhost:" + std::to_string(server.port()))).status == 400,
          "reference data rejects a noncanonical loopback Host");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root, current_token,
              "http://evil.invalid")).status == 403,
          "reference data rejects an explicit foreign Origin");
    Check(Request(server.port(), ReferenceGet(
              server.port(), current_root, settings.ReferenceCapability(old_id)))
              .status == 403,
          "reference read rejects a capability scoped to another ID");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + unknown_id,
              settings.ReferenceCapability(unknown_id))).status == 404,
          "authorized unknown reference is distinct");
    const std::string pending_id(32, 'd');
    Reference pending;
    pending.id = pending_id;
    pending.mark_id = pending_id;
    pending.context = ContextFixture(Now());
    pending.path = {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1},
                    {1, CoordinateUnit::kLogicalPoints, {40, 40}, 1}};
    pending.circle_completed = Now();
    Check(references.AcceptPending(pending, 10, 20).state ==
              ReferenceJobState::kPending,
          "service fixture accepts a pending identity");
    const auto pending_url = server.ClipboardText(pending_id);
    const std::string pending_path = "/r/" + pending_id +
        settings.ReferenceCapability(pending_id);
    Check(pending_url.find_first_of(" \t\r\n") == std::string::npos &&
              pending_url == server.base_url() + pending_path,
          "pending publication is one direct JSON capability URL");
    response = Request(server.port(), ReferenceGet(server.port(), pending_path));
    Check(response.status == 202 &&
              response.body.find("\"state\":\"pending\"") != std::string::npos &&
              response.body.find("\"retry_ms\":25") != std::string::npos,
          "pending JSON capability remains directly retryable");
    const auto pending_start = std::chrono::steady_clock::now();
    response = Request(server.port(), ReferenceGet(
        server.port(), "/v1/references/" + pending_id,
        settings.ReferenceCapability(pending_id)));
    const auto pending_elapsed = std::chrono::steady_clock::now() - pending_start;
    Check(response.status == 202 &&
              response.body.find("\"state\":\"pending\"") != std::string::npos &&
              response.body.find("\"retry_ms\":25") != std::string::npos,
          "same scoped ID reports machine-readable pending state");
    Check(pending_elapsed < std::chrono::milliseconds(500),
          "pending status stays on the lightweight index path");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + pending_id + "/crop.png",
              settings.ReferenceCapability(pending_id))).status == 202,
          "pending asset keeps the same retryable identity");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + pending_id + "/annotated.png",
              settings.ReferenceCapability(pending_id))).status == 202,
          "pending annotated asset keeps the same retryable identity");
    Check(Request(server.port(), ReferenceGet(
              server.port(), pending_path + "/crop.png")).status == 202 &&
              Request(server.port(), ReferenceGet(
              server.port(), pending_path + "/annotated.png")).status == 202,
          "pending assets share the JSON path capability");
    references.Fail(pending_id, "capture_failed");
    references.WaitForIdleForTesting();
    response = Request(server.port(), ReferenceGet(
        server.port(), "/v1/references/" + pending_id,
        settings.ReferenceCapability(pending_id)));
    Check(response.status == 422 &&
              response.body.find("\"state\":\"failed\"") != std::string::npos &&
              response.body.find("capture_failed") != std::string::npos,
          "terminal failure remains explicit at the same scoped ID");
    Check(server.ClipboardText(pending_id) == pending_url,
          "failed state retains the exact pending URL identity");
    Check(Request(server.port(), ReferenceGet(server.port(), pending_path)).status == 422,
          "failed JSON capability reports the same terminal lifecycle");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/not-an-id", "x")).status == 400,
          "invalid reference identity is rejected");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/../settings", "x")).status == 400,
          "traversal target is rejected");

    const auto cold_metadata_start = std::chrono::steady_clock::now();
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root, current_token));
    const auto cold_metadata_elapsed = std::chrono::steady_clock::now() -
        cold_metadata_start;
    Check(response.status == 200 &&
              response.body.starts_with("{\"schema\":1,") &&
              response.body.find("Immutable fixture") != std::string::npos,
          "legacy metadata returns its persisted schema and immutable context");
    Check(cold_metadata_elapsed < std::chrono::milliseconds(500),
          "cold metadata stays inside the viewer request bound");
    std::cout << "cold_metadata_initial_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     cold_metadata_elapsed).count() << '\n';
    Check(response.body.find(current.value.source.sha256) != std::string::npos &&
          response.body.find("crop_pixels") != std::string::npos,
          "metadata returns source and crop identity");
    const auto multi_response = Request(server.port(), ReferenceGet(
        server.port(), multi_root, multi_token));
    Check(multi_response.status == 200 &&
              multi_response.body.starts_with("{\"schema\":2,") &&
              multi_response.body.find("\"record_url\":null") !=
                  std::string::npos &&
              multi_response.body.find("\"sha256\":null,\"encoding\":"
                  "\"png-zlib-v1\",\"pixel_integrity\":{\"contract\":"
                  "\"rgba8-sha256-v1\"") != std::string::npos &&
              multi_response.body.find(multi.value.crop.pixel_sha256) !=
                  std::string::npos &&
              multi_response.body.find("display-b") != std::string::npos &&
              multi_response.body.find("\"regions\":[{\"region\":") !=
                  std::string::npos &&
              multi_response.body.find("},{\"region\":") !=
                  std::string::npos,
          "multi-region metadata returns schema 2 with independent paths and bounds");
    Check(multi_response.body.find("\"annotated\":{\"width\":100,\"height\":100,")!=
              std::string::npos&&
              multi_response.body.find(multi_root+"/annotated.png")!=
                  std::string::npos&&
              multi_response.body.find("\"state\":\"pending\",\"sha256\":null")!=
                  std::string::npos,
          "metadata starts bounded annotated derivation with URL and truthful pending digest");
    references.WaitForIdleForTesting();
    const auto annotated_start=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/annotated.png",multi_token));
    Check(annotated_start.status==202,
          "queued annotated identity advances to its own bounded derivation");
    references.WaitForIdleForTesting();
    const auto annotated_response=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/annotated.png",multi_token));
    const auto annotated_bytes=Bytes(annotated_response.body.begin(),
                                     annotated_response.body.end());
    const auto annotated_hash=Sha256(annotated_bytes);
    const auto annotated_metadata=Request(server.port(),ReferenceGet(
        server.port(),multi_root,multi_token));
    const auto annotated_repeat=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/annotated.png",multi_token));
    Check(annotated_response.status==200&&annotated_repeat.status==200&&
              Header(annotated_response,"Content-Type")=="image/png"&&
              Header(annotated_response,"ETag")=="\"sha256-"+annotated_hash+"\""&&
              Header(annotated_response,"X-SeeThis-PNG-SHA256")==annotated_hash&&
              annotated_repeat.body==annotated_response.body&&
              annotated_metadata.body.find("\"state\":\"ready\",\"sha256\":\""+
                                           annotated_hash+"\"")!=std::string::npos&&
              FileBytes(reference_root/(multi_id+".annotated.png"))==
                  annotated_bytes,
          "annotated endpoint serves stable authenticated PNG bytes and metadata digest");
    Check(Request(server.port(),ReferenceGet(
              server.port(),multi_root+"/annotated.png",current_token)).status==403,
          "annotated image rejects a capability scoped to another reference");
    const auto primary_source=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/source.png",multi_token));
    const auto primary_bytes=Bytes(primary_source.body.begin(),primary_source.body.end());
    Check(primary_source.status==200&&primary_bytes==multi.value.source.bytes&&
              Header(primary_source,"Content-Type")=="image/png"&&
              primary_bytes.size()>=8&&primary_bytes[0]==137&&
              primary_bytes[1]==80&&primary_bytes[2]==78&&primary_bytes[3]==71&&
              Header(primary_source,"X-SeeThis-PNG-SHA256")==Sha256(primary_bytes)&&
              Header(primary_source,"ETag")=="\"sha256-"+Sha256(primary_bytes)+"\"",
          "schema-2 primary viewer asset is the persisted full-context PNG");
    const auto regions_start = multi_response.body.find("\"regions\":[");
    const auto first_region = multi_response.body.find(
        "\"crop_pixels\":{\"x\":", regions_start);
    const auto second_region = multi_response.body.find(
        "\"crop_pixels\":{\"x\":", first_region + 1);
    Check(regions_start != std::string::npos &&
              first_region != std::string::npos && second_region != std::string::npos &&
              first_region != second_region &&
              multi_response.body.find("\"subpaths\":[[{\"display_id\":1") !=
                  std::string::npos,
          "schema-2 metadata exposes executable per-region source geometry and paths");
    Check(Request(server.port(), ReferenceGet(
              server.port(), multi_root + "/record", multi_token)).status == 422,
          "schema-2 URL-only references do not materialize a legacy record export");
    std::mutex derive_mutex;
    std::condition_variable derive_ready;
    bool derive_entered=false,allow_derive=false,derive_identity_matches=true;
    unsigned derive_starts=0;
    ReferenceStoreTestHooks derive_hooks;
    derive_hooks.before_derive_crop=[&](std::string_view id) {
      std::unique_lock lock(derive_mutex);
      derive_identity_matches=derive_identity_matches&&
          (id==multi_id||id==queued_multi_id);
      ++derive_starts;derive_entered=true;derive_ready.notify_all();
      derive_ready.wait(lock,[&]{return allow_derive;});
    };
    references.SetTestHooks(std::move(derive_hooks));
    Check(derive_starts==0,
          "metadata and full-context source readiness do not derive an unused crop");
    const auto first_crop_start=std::chrono::steady_clock::now();
    const auto first_crop=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/crop.png",multi_token));
    const auto first_crop_us=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-first_crop_start).count();
    {
      std::unique_lock lock(derive_mutex);
      Check(derive_ready.wait_for(lock,std::chrono::seconds(2),
                                  [&]{return derive_entered;}),
            "cold crop enters the bounded background executor");
    }
    const auto held_start=std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(2100));
    const auto held_us=std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now()-held_start).count();
    const auto metadata_while_deriving_start=std::chrono::steady_clock::now();
    const auto metadata_while_deriving=Request(server.port(),ReferenceGet(
        server.port(),multi_root,multi_token));
    const auto metadata_while_deriving_us=
        std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now()-metadata_while_deriving_start).count();
    const auto repeated_crop=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/crop.png",multi_token));
    const auto queued_crop=Request(server.port(),ReferenceGet(
        server.port(),queued_multi_root+"/crop.png",queued_multi_token));
    {
      std::lock_guard lock(derive_mutex);
      Check(first_crop.status==202&&first_crop.body.find("\"state\":\"deriving\"")!=
                std::string::npos&&first_crop_us<500'000&&
            metadata_while_deriving.status==200&&
                metadata_while_deriving_us<500'000&&
            repeated_crop.status==202&&derive_starts==1&&
                queued_crop.status==202&&
                queued_crop.body.find("\"state\":\"queued\"")!=
                    std::string::npos&&
                held_us>=2'000'000&&
                derive_identity_matches,
            "cold crop stays truthful beyond the old two-second request bound");
      allow_derive=true;
    }
    derive_ready.notify_all();references.WaitForIdleForTesting();
    const auto completed_crop=Request(server.port(),ReferenceGet(
        server.port(),multi_root+"/crop.png",multi_token));
    const auto completed_bytes=Bytes(completed_crop.body.begin(),completed_crop.body.end());
    const auto completed_hash=Sha256(completed_bytes);
    Check(completed_crop.status==200&&!completed_bytes.empty()&&
              Header(completed_crop,"X-SeeThis-PNG-SHA256")==completed_hash&&
              Header(completed_crop,"ETag")=="\"sha256-"+completed_hash+"\""&&
              completed_hash!=multi.value.crop.pixel_sha256,
          "crop polling returns bytes matching their own SHA/ETag, distinct from pixel integrity");
    const auto queued_derivation=Request(server.port(),ReferenceGet(
        server.port(),queued_multi_root+"/crop.png",queued_multi_token));
    references.WaitForIdleForTesting();
    const auto queued_completed=Request(server.port(),ReferenceGet(
        server.port(),queued_multi_root+"/crop.png",queued_multi_token));
    unsigned completed_derivations=0;
    bool completed_identities_match=false;
    {std::lock_guard lock(derive_mutex);
      completed_derivations=derive_starts;
      completed_identities_match=derive_identity_matches;}
    const auto queued_bytes=Bytes(queued_completed.body.begin(),queued_completed.body.end());
    const auto queued_hash=Sha256(queued_bytes);
    Check(queued_derivation.status==202&&queued_completed.status==200&&
              !queued_bytes.empty()&&
              Header(queued_completed,"X-SeeThis-PNG-SHA256")==queued_hash&&
              Header(queued_completed,"ETag")=="\"sha256-"+queued_hash+"\""&&
              queued_hash!=queued_multi.value.crop.pixel_sha256&&completed_derivations==2&&
                  completed_identities_match,
          "queued identity advances through one bounded derivation slot to exact PNG");
    std::cout<<"cold_schema2_crop_protocol,statuses="<<first_crop.status<<','
             <<repeated_crop.status<<','<<queued_crop.status<<','
             <<completed_crop.status<<','<<queued_derivation.status<<','
             <<queued_completed.status
             <<",first_response_us="<<first_crop_us
             <<",metadata_while_deriving_us="<<metadata_while_deriving_us
             <<",blocked_derivation_us="<<held_us
             <<",derive_starts="<<completed_derivations<<'\n';
    references.SetTestHooks({});
    Check(references.Delete(multi_id)==DeleteResult::kDeleted&&
              Request(server.port(),ReferenceGet(
                server.port(),multi_root+"/crop.png",multi_token)).status==410&&
              Request(server.port(),ReferenceGet(
                server.port(),multi_root+"/annotated.png",multi_token)).status==410&&
              !std::filesystem::exists(reference_root/(multi_id+".annotated.png")),
          "schema-2 deletion invalidates both derived routes and annotated cache");
    std::vector<std::pair<int,long long>> metadata_attempts;
    for (int attempt = 0; attempt < 220; ++attempt) {
      const auto start = std::chrono::steady_clock::now();
      const auto measured = Request(server.port(), ReferenceGet(
          server.port(), current_root, current_token));
      const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start).count();
      if (attempt >= 20) metadata_attempts.push_back({measured.status,elapsed});
    }
    std::vector<long long> sorted_metadata;
    bool every_metadata_succeeded=true;
    for(std::size_t index=0;index<metadata_attempts.size();++index) {
      const auto [status,elapsed]=metadata_attempts[index];
      std::cout<<"cold_metadata_attempt,"<<index<<",status="<<status
               <<",elapsed_us="<<elapsed<<'\n';
      every_metadata_succeeded=every_metadata_succeeded&&status==200;
      sorted_metadata.push_back(elapsed);
    }
    std::sort(sorted_metadata.begin(), sorted_metadata.end());
    std::cout << "cold_metadata_p95_us=" << sorted_metadata[189] << '\n';
    Check(metadata_attempts.size()==200&&every_metadata_succeeded&&
              sorted_metadata[189] < 500'000,
          "200-attempt cold metadata p95 remains within viewer bounds");
    Check(!references.LookupMetadata(current_id)->ready,
          "metadata cohort never warms the full stored payload");
    const auto crop_start = std::chrono::steady_clock::now();
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root + "/crop.png", current_token));
    const auto crop_elapsed = std::chrono::steady_clock::now() - crop_start;
    const auto cold_crop_bytes=Bytes(response.body.begin(),response.body.end());
    Check(response.status == 200 &&
              cold_crop_bytes==current.value.crop.bytes&&
              Header(response,"X-SeeThis-PNG-SHA256")==Sha256(cold_crop_bytes)&&
              Header(response,"ETag")=="\"sha256-"+Sha256(cold_crop_bytes)+"\"",
          "following cold crop returns exact immutable PNG bytes without record prewarm");
    Check(crop_elapsed < std::chrono::seconds(2),
          "following cold crop reaches verified bytes within viewer bounds");
    std::cout << "cold_crop_us="
              << std::chrono::duration_cast<std::chrono::microseconds>(
                     crop_elapsed).count() << '\n';
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root + "/record", current_token));
    Check(response.status == 200 && Bytes(response.body.begin(), response.body.end()) == current.record,
          "record endpoint returns exact immutable envelope bytes");
    response = Request(server.port(), ReferenceGet(
        server.port(), current_root + "/source.png", current_token));
    const auto source_bytes=Bytes(response.body.begin(),response.body.end());
    Check(response.status == 200 && source_bytes==current.value.source.bytes&&
              Header(response,"X-SeeThis-PNG-SHA256")==Sha256(source_bytes)&&
              Header(response,"ETag")=="\"sha256-"+Sha256(source_bytes)+"\"",
          "source endpoint returns exact immutable PNG bytes");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + old_id,
              settings.ReferenceCapability(old_id))).status == 410,
          "expired reference is distinct from unknown reference");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/r/" + old_id +
              settings.ReferenceCapability(old_id))).status == 410,
          "expired path capability reports the same gone lifecycle");

    const std::string deleted_root="/v1/references/"+deleted_id;
    const std::string deleted_token=settings.ReferenceCapability(deleted_id);
    const std::string deleted_agent_root="/r/"+deleted_id+deleted_token;
    Check(references.Delete(deleted_id)==DeleteResult::kDeleted,
          "service deletion fixture invalidates the ready reference cache");
    for(const auto& suffix:{"","/record","/source.png","/crop.png"}) {
      response=Request(server.port(),ReferenceGet(
          server.port(),deleted_root+suffix,deleted_token));
      Check(response.status==410&&
                response.body.find("\"state\":\"deleted\"")!=std::string::npos&&
                response.body.find("deleted_reference")!=std::string::npos,
            "old authenticated route reports truthful deleted state");
    }
    for(const auto& suffix:{"","/record","/source.png","/crop.png",
                            "/annotated.png"}) {
      response=Request(server.port(),ReferenceGet(
          server.port(),deleted_agent_root+suffix));
      Check(response.status==410&&
                response.body.find("\"state\":\"deleted\"")!=std::string::npos,
            "deletion invalidates JSON capability and every agent asset");
    }
    Check(Request(server.port(),ReferenceGet(
              server.port(),deleted_root,"wrong")).status==403,
          "deletion does not weaken scoped read authentication");
    Check(Request(server.port(),ReferenceGet(
              server.port(),deleted_root+"/view")).status==200,
          "old compact URL retains the generic shell that resolves authenticated gone state");
    Check(Request(server.port(),"DELETE "+deleted_root+
              " HTTP/1.1\r\nHost: 127.0.0.1:"+
              std::to_string(server.port())+"\r\nAuthorization: Bearer "+
              deleted_token+"\r\n\r\n").status==405,
          "existing read capability gains no delete authority");

    std::string post = "POST /v1/references/" + current_id +
                       " HTTP/1.1\r\nHost: 127.0.0.1:" +
                       std::to_string(server.port()) + "\r\n\r\n";
    Check(Request(server.port(), post).status == 405,
          "reference mutation method is rejected");
    Check(Request(server.port(), "GET /v1/settings HTTP/1.1\r\nHost: a\r\nHost: b\r\n\r\n").status == 400,
          "duplicate headers are rejected");
    Check(Request(server.port(), "GET /v1/settings HTTP/1.1\r\nHost: a\r\nTransfer-Encoding: chunked\r\n\r\n").status == 400,
          "transfer encoding is rejected");
    Check(Request(server.port(), "PUT /v1/settings HTTP/1.1\r\nHost: a\r\nContent-Length: 16385\r\n\r\n").status == 413,
          "oversized request body is rejected before reading it");
    std::string high_bit_length =
        "GET /v1/settings HTTP/1.1\r\nHost: a\r\nContent-Length: ";
    high_bit_length.push_back(static_cast<char>(0x80));
    high_bit_length += "\r\n\r\n";
    Check(Request(server.port(), high_bit_length).status == 400,
          "high-bit Content-Length byte is rejected safely");
    Check(Request(server.port(), Get("/v1/settings", settings_token)).status == 200,
          "server remains available after high-bit Content-Length rejection");
    Check(Request(server.port(), "GET /v1/settings HTTP/1.1\r\nHost: a\r\n\r\nx").status == 400,
          "undeclared request body is rejected");
    Check(Request(server.port(), "BROKEN\r\n\r\n").status == 400,
          "malformed request line is rejected");
    Check(Request(server.port(), "GET /v1/settings HTTP/1.1\nHost: a\r\n\r\n").status == 400,
          "bare line feed is rejected");
    Check(Request(server.port(), Get("/unknown")).status == 404,
          "unknown endpoint is explicit");

    const auto clipboard = server.ClipboardText(current);
    const std::string expected_clipboard = server.base_url() +
        "/r/" + current_id + current_token;
    Check(clipboard == expected_clipboard &&
              clipboard.find_first_of(" \t\r\n") == std::string::npos &&
              clipboard.size() <
                  seethis::service::ReferenceServer::kMaximumClipboardTextBytes,
          "clipboard publication is exactly one authenticated bounded URL");
    const std::string maximum_port_clipboard =
        "http://127.0.0.1:65535/r/" + current_id + current_token;
    Check(maximum_port_clipboard.size() <
                  seethis::service::ReferenceServer::kMaximumClipboardTextBytes,
          "maximum-port reference URL remains below two KiB");
    Check(cold_record_size >= 27ULL * 1024 * 1024,
          "URL-only copy fixture has a former record of at least 27 MiB");
    Check(clipboard.size() * 1000 < cold_record_size &&
              clipboard.find("SeeThis reference") == std::string::npos &&
              clipboard.find("hex-stref1") == std::string::npos &&
              (current.value.source.sha256.empty() ||
               clipboard.find(current.value.source.sha256) == std::string::npos),
          "former record, title, and source cannot inflate pasted URL text");
    Check(current.clipboard_png == current.value.crop.bytes,
          "legacy durable crop bytes remain available outside URL-only publication");
    Check(multi.clipboard_png.empty() && multi.clipboard_text.size() < 256,
          "schema-2 URL path stores no legacy PNG or embedded record payload");
    Check(clipboard == server.ClipboardText(current_id),
          "historical re-copy preserves stable authenticated URL identity");

    const auto parsed = ParseCompactHandoff(server.ViewerUrl(current_id),
                                           server.base_url());
    Check(parsed.target == current_root + "/view" &&
              parsed.capability == current_token &&
              parsed.target.find('#') == std::string::npos,
          "viewer compatibility URL retains fragment capability");
    const std::string parsed_root = parsed.target.substr(
        0, parsed.target.size() - std::string_view("/view").size());
    response = Request(server.port(), ReferenceGet(
        server.port(), parsed_root, parsed.capability));
    Check(response.status == 200 &&
              response.body.find("\"reference_id\":\"" + current_id + "\"") !=
                  std::string::npos &&
              response.body.find(current.value.crop.sha256) != std::string::npos,
          "local tool uses parsed bearer for authenticated metadata");
    response = Request(server.port(), ReferenceGet(
        server.port(), parsed_root + "/crop.png", parsed.capability));
    const auto parsed_crop=Bytes(response.body.begin(),response.body.end());
    Check(response.status == 200 && parsed_crop==current.value.crop.bytes&&
              Header(response,"X-SeeThis-PNG-SHA256")==Sha256(parsed_crop)&&
              Header(response,"ETag")=="\"sha256-"+Sha256(parsed_crop)+"\"",
          "legacy Bearer path still retrieves the exact authenticated PNG");

    const std::string agent_root = "/r/" + current_id + current_token;
    response = Request(server.port(), ReferenceGet(server.port(), agent_root));
    Check(response.status == 200 &&
              Header(response,"Cache-Control")=="no-store" &&
              Header(response,"Referrer-Policy")=="no-referrer" &&
              response.body.find("\"id\":\""+current_id+"\"")!=std::string::npos &&
              response.body.find("\"state\":\"ready\"")!=std::string::npos &&
              response.body.find("\"context\":{")!=std::string::npos &&
              response.body.find("\"regions\":[")!=std::string::npos &&
              response.body.find("\"annotated_url\":\""+server.base_url()+
                                 agent_root+"/annotated.png\"")!=std::string::npos &&
              response.body.find("\"source_url\":\""+server.base_url()+
                                 agent_root+"/source.png\"")!=std::string::npos &&
              response.body.find("\"crop_url\":\""+server.base_url()+
                                 agent_root+"/crop.png\"")!=std::string::npos &&
              response.body.find("\"source_sha256\":\""+
                                 current.value.source.sha256+"\"")!=std::string::npos &&
              response.body.find("\"crop_sha256\":\""+
                                 current.value.crop.sha256+"\"")!=std::string::npos,
          "one GET without separate Bearer of pasted capability returns agent JSON");
    for(const auto& suffix:{"/source.png","/crop.png","/annotated.png"}) {
      auto asset = Request(server.port(), ReferenceGet(
          server.port(), agent_root + suffix));
      if (asset.status == 202) {
        references.WaitForIdleForTesting();
        asset = Request(server.port(), ReferenceGet(
            server.port(), agent_root + suffix));
      }
      Check(asset.status == 200 && Header(asset,"X-SeeThis-PNG-SHA256")==
                Sha256(Bytes(asset.body.begin(),asset.body.end())),
            "same path capability fetches each exact PNG asset");
    }
    const std::string wrong_agent_root = "/r/" + current_id +
        settings.ReferenceCapability(old_id);
    Check(Request(server.port(), ReferenceGet(
              server.port(), wrong_agent_root)).status == 403 &&
              Request(server.port(), ReferenceGet(
              server.port(), "/r/"+current_id)).status == 400 &&
              Request(server.port(), ReferenceGet(
              server.port(), agent_root.substr(0, agent_root.size()-1))).status == 400 &&
              Request(server.port(), ReferenceGet(
              server.port(), agent_root+"/view")).status == 400 &&
              Request(server.port(), ReferenceGet(
              server.port(), agent_root, {}, server.base_url()+"/other")).status == 403 &&
              Request(server.port(), ReferenceGet(
              server.port(), agent_root, {}, std::nullopt,
              "localhost:"+std::to_string(server.port()))).status == 400,
          "path capability stays scoped and Host/Origin constrained");

    { std::ofstream damaged(reference_root / (current_id + ".source.png"),
                            std::ios::binary | std::ios::app);
      damaged << "hash-mismatch"; }
    Response damaged_asset;
    std::thread slow_asset([&] {
      damaged_asset = Request(server.port(), ReferenceGet(
          server.port(), current_root + "/source.png", current_token));
    });
    const auto concurrent_start = std::chrono::steady_clock::now();
    const auto concurrent_metadata = Request(server.port(), ReferenceGet(
        server.port(), current_root, current_token));
    const auto concurrent_elapsed = std::chrono::steady_clock::now() -
        concurrent_start;
    slow_asset.join();
    Check(damaged_asset.status == 422 &&
              damaged_asset.body.find("asset_unavailable") != std::string::npos,
          "hash-mismatched cold asset fails honestly");
    Check(concurrent_metadata.status == 200 &&
              concurrent_elapsed < std::chrono::milliseconds(500),
          "metadata stays responsive while another worker validates a large corrupt asset");
    Check(std::filesystem::remove(reference_root / (current_id + ".source.png")) &&
              Request(server.port(), ReferenceGet(
                  server.port(), current_root + "/source.png", current_token)).status == 422,
          "missing cold asset fails honestly without weakening the route");

    changed.maximum_visible_references = 1;
    changed.reference_ttl_seconds = 3600;
    Check(settings.Update(changed, &parse_error), "capacity settings update succeeds");
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + old_id,
              settings.ReferenceCapability(old_id))).status == 410,
          "references outside visible storage limit expire explicitly");
    auto terminal = [&](char digit) {
      Reference reference;
      reference.id = std::string(32, digit);
      reference.context = ContextFixture(Now());
      reference.path = {{1, CoordinateUnit::kLogicalPoints, {20, 20}, 1}};
      reference.circle_completed = Now();
      auto accepted = references.AcceptPending(reference, 1, 2);
      Check(accepted.state == ReferenceJobState::kPending,
            "terminal retention service fixture accepts");
      references.Fail(reference.id, "cancelled");
      references.WaitForIdleForTesting();
      return reference.id;
    };
    const auto first_failed = terminal('e');
    const auto second_failed = terminal('f');
    const auto expired_failed = Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + first_failed,
              settings.ReferenceCapability(first_failed)));
    Check(expired_failed.status == 410 &&
              expired_failed.body.find("\"state\":\"expired\"") !=
                  std::string::npos,
          "failed terminal identity becomes an authenticated expired tombstone");
    const auto third_failed = terminal('0');
    (void)second_failed;
    (void)third_failed;
    Check(Request(server.port(), ReferenceGet(
              server.port(), "/v1/references/" + first_failed,
              settings.ReferenceCapability(first_failed))).status == 404,
          "oldest failed tombstone eventually becomes unknown");
    server.Stop();
    Check(server.port() == 0, "server stops with app-owned lifetime");
    std::cout << checks << " reference server/settings checks passed\n";
    return 0;
  } catch (const std::exception& exception) {
    std::cerr << "FAILED after " << checks << " checks: " << exception.what() << '\n';
    return 1;
  }
}

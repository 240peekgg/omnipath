#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>
#include <Geode/utils/hash.hpp>
#include <Geode/utils/web.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <optional>
#include <string>

using namespace geode::prelude;

namespace {

constexpr char kManifestUrl[] =
    "https://raw.githubusercontent.com/240peekgg/omnipath/main/dist/update.txt";

struct UpdateManifest {
    std::string version;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
};

async::TaskHolder<web::WebResponse> g_manifestTask;
async::TaskHolder<web::WebResponse> g_downloadTask;
bool g_checkedThisLaunch = false;
bool g_updateInProgress = false;

std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

std::optional<std::string> field(std::string const& body, std::string const& key) {
    auto prefix = key + "=";
    std::size_t pos = 0;

    while (pos < body.size()) {
        auto end = body.find('\n', pos);
        if (end == std::string::npos) end = body.size();
        auto line = trim(body.substr(pos, end - pos));
        if (line.starts_with(prefix))
            return trim(line.substr(prefix.size()));
        pos = end + 1;
    }
    return std::nullopt;
}

std::optional<UpdateManifest> parseManifest(std::string const& body) {
    auto version = field(body, "version");
    auto url = field(body, "url");
    auto sha = field(body, "sha256");
    auto sizeText = field(body, "size");
    if (!version || !url || !sha || !sizeText) return std::nullopt;

    UpdateManifest result;
    result.version = *version;
    result.url = *url;
    result.sha256 = *sha;
    std::transform(result.sha256.begin(), result.sha256.end(), result.sha256.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

    try {
        result.size = std::stoull(*sizeText);
    } catch (...) {
        return std::nullopt;
    }

    if (result.version.empty() || result.url.empty() || result.sha256.size() != 64 || result.size < 1024)
        return std::nullopt;

    return result;
}

void notifyInstalled(std::string version) {
    queueInMainThread([version = std::move(version)] {
        Notification::create(
            fmt::format("OmniPath {} installed - restart GD", version),
            static_cast<CCNode*>(nullptr),
            5.0f
        )->show();
    });
}

void downloadUpdate(UpdateManifest manifest) {
    if (g_updateInProgress) return;
    g_updateInProgress = true;

    auto request = web::WebRequest();
    request.header("Cache-Control", "no-cache");
    request.userAgent(fmt::format("OmniPath/{}", Mod::get()->getVersion().toVString()));
    request.timeout(std::chrono::seconds(30));

    g_downloadTask.spawn(
        request.get(manifest.url),
        [manifest = std::move(manifest)](web::WebResponse response) mutable {
            g_updateInProgress = false;

            if (!response.ok()) {
                log::warn("OmniPath updater: download failed with HTTP {}: {}",
                    response.code(), response.errorMessage());
                return;
            }

            auto const& bytes = response.data();
            if (bytes.size() != manifest.size) {
                log::warn("OmniPath updater: size mismatch, expected {}, got {}",
                    manifest.size, bytes.size());
                return;
            }

            auto actualHash = geode::sha256(bytes).toString();
            if (actualHash != manifest.sha256) {
                log::error("OmniPath updater: SHA256 mismatch, refusing update");
                return;
            }

            auto package = Mod::get()->getPackagePath();
            if (package.empty()) {
                log::error("OmniPath updater: current package path is empty");
                return;
            }

            auto staged = package.parent_path() /
                (package.filename().string() + ".omnipath-update");

            std::error_code ec;
            std::filesystem::remove(staged, ec);

            auto writeResult = response.into(staged);
            if (writeResult.isErr()) {
                log::error("OmniPath updater: failed to stage update: {}", writeResult.unwrapErr());
                return;
            }

            auto metadata = ModMetadata::createFromGeodeFile(staged);
            if (metadata.wasCompletelyUnparseable() || metadata.hasErrors()) {
                log::error("OmniPath updater: downloaded file is not a valid .geode package");
                std::filesystem::remove(staged, ec);
                return;
            }

            if (metadata.getID() != Mod::get()->getID()) {
                log::error("OmniPath updater: downloaded package has wrong mod id: {}", metadata.getID());
                std::filesystem::remove(staged, ec);
                return;
            }

            auto expectedVersion = VersionInfo::parse(manifest.version);
            if (expectedVersion.isErr() || metadata.getVersion() != expectedVersion.unwrap()) {
                log::error("OmniPath updater: downloaded package version does not match manifest");
                std::filesystem::remove(staged, ec);
                return;
            }

            auto compatibility = metadata.checkTargetVersions();
            if (compatibility.isErr()) {
                log::error("OmniPath updater: downloaded package is incompatible: {}",
                    compatibility.unwrapErr());
                std::filesystem::remove(staged, ec);
                return;
            }

#ifdef GEODE_IS_ANDROID
            // staged lives next to the real package, so POSIX rename is an atomic
            // same-filesystem replacement on Android. The currently loaded .so is
            // already extracted by Geode, so the new package is picked up next launch.
            std::filesystem::rename(staged, package, ec);
            if (ec) {
                log::error("OmniPath updater: atomic package replacement failed: {}", ec.message());
                std::filesystem::remove(staged, ec);
                return;
            }
#else
            log::warn("OmniPath updater: self-install is currently enabled only on Android");
            std::filesystem::remove(staged, ec);
            return;
#endif

            Mod::get()->setSavedValue("last-auto-update", manifest.version);
            log::info("OmniPath updater: installed {}, restart required", manifest.version);
            notifyInstalled(manifest.version);
        }
    );
}

void checkForGitHubUpdate() {
    if (g_checkedThisLaunch) return;
    g_checkedThisLaunch = true;

    auto request = web::WebRequest();
    request.header("Cache-Control", "no-cache");
    request.userAgent(fmt::format("OmniPath/{}", Mod::get()->getVersion().toVString()));
    request.timeout(std::chrono::seconds(12));

    g_manifestTask.spawn(
        request.get(kManifestUrl),
        [](web::WebResponse response) {
            if (!response.ok()) {
                log::debug("OmniPath updater: manifest request failed with HTTP {}", response.code());
                return;
            }

            auto text = response.string();
            if (text.isErr()) {
                log::warn("OmniPath updater: manifest is not text");
                return;
            }

            auto manifest = parseManifest(text.unwrap());
            if (!manifest) {
                log::warn("OmniPath updater: malformed update manifest");
                return;
            }

            auto remote = VersionInfo::parse(manifest->version);
            if (remote.isErr()) {
                log::warn("OmniPath updater: invalid remote version {}", manifest->version);
                return;
            }

            auto local = Mod::get()->getVersion();
            if (remote.unwrap() <= local) {
                log::debug("OmniPath updater: current {} is up to date", local);
                return;
            }

            log::info("OmniPath updater: {} -> {}, downloading", local, manifest->version);
            downloadUpdate(std::move(*manifest));
        }
    );
}

} // namespace

class $modify(OmniPathUpdaterMenuLayer, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        checkForGitHubUpdate();
        return true;
    }
};

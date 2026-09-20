#include <UE4SSLuaEventBridge/LegacyInstallMigration.hpp>

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

namespace
{
std::filesystem::path unique_root()
{
    const auto suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("ue4ss-leb-migration-" + std::to_string(suffix));
}

void write(const std::filesystem::path& path, std::string_view value)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream << value;
    assert(stream.good());
}

std::string read(const std::filesystem::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
}

int main()
{
    using UE4SSLuaEventBridge::LegacyInstallMigrationResult;
    using UE4SSLuaEventBridge::migrate_legacy_install;

    const auto root = unique_root();
    const auto current = root / "_UE4SSLuaEventBridge" / "dlls" / "main.dll";
    std::filesystem::create_directories(current.parent_path());

    assert(migrate_legacy_install(current) == LegacyInstallMigrationResult::no_legacy_install);

    const auto legacy = root / "UE4SSLuaEventBridge";
    std::filesystem::create_directories(legacy);
    assert(migrate_legacy_install(current) == LegacyInstallMigrationResult::migrated);
    assert(read(legacy / "deprecated.txt") == "_UE4SSLuaEventBridge");

    std::filesystem::remove(legacy / "deprecated.txt");
    write(legacy / "enabled.txt", "enabled");
    assert(migrate_legacy_install(current) == LegacyInstallMigrationResult::migrated);
    assert(!std::filesystem::exists(legacy / "enabled.txt"));
    assert(read(legacy / "deprecated.txt") == "_UE4SSLuaEventBridge");
    assert(!std::filesystem::exists(legacy / "deprecated.txt.tmp"));

    write(legacy / "enabled.txt", "preserve after migration");
    assert(migrate_legacy_install(current) == LegacyInstallMigrationResult::already_migrated);
    assert(read(legacy / "enabled.txt") == "preserve after migration");
    assert(read(legacy / "deprecated.txt") == "_UE4SSLuaEventBridge");

    const auto legacy_location = root / "UE4SSLuaEventBridge" / "dlls" / "main.dll";
    assert(migrate_legacy_install(legacy_location) ==
           LegacyInstallMigrationResult::unexpected_current_location);

    std::filesystem::remove_all(root);
}

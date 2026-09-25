#pragma once

#include <filesystem>
#include <fstream>
#include <string_view>
#include <system_error>

namespace UE4SSLuaEventBridge
{
inline constexpr std::wstring_view current_mod_folder = L"_ModCore_UE4SSLuaEventBridge";
inline constexpr std::wstring_view legacy_mod_folder = L"UE4SSLuaEventBridge";
inline constexpr std::string_view legacy_deprecation_target = "_ModCore_UE4SSLuaEventBridge";

enum class LegacyInstallMigrationResult
{
    no_legacy_install,
    already_migrated,
    migrated,
    unexpected_current_location,
    failed,
};

inline LegacyInstallMigrationResult migrate_legacy_install(
    const std::filesystem::path& current_module_file) noexcept
{
    try
    {
        const auto current_mod = current_module_file.parent_path().parent_path();
        if (current_mod.filename().wstring() != current_mod_folder)
        {
            return LegacyInstallMigrationResult::unexpected_current_location;
        }

        const auto legacy_mod = current_mod.parent_path() / legacy_mod_folder;
        std::error_code error;
        if (!std::filesystem::exists(legacy_mod, error))
        {
            return error ? LegacyInstallMigrationResult::failed
                         : LegacyInstallMigrationResult::no_legacy_install;
        }
        if (!std::filesystem::is_directory(legacy_mod, error) || error)
        {
            return LegacyInstallMigrationResult::failed;
        }

        const auto marker = legacy_mod / L"deprecated.txt";
        const auto marker_exists = std::filesystem::exists(marker, error);
        if (error)
        {
            return LegacyInstallMigrationResult::failed;
        }
        if (marker_exists && std::filesystem::is_regular_file(marker, error))
        {
            return LegacyInstallMigrationResult::already_migrated;
        }
        if (error || marker_exists)
        {
            return LegacyInstallMigrationResult::failed;
        }

        const auto enabled = legacy_mod / L"enabled.txt";
        if (std::filesystem::exists(enabled, error))
        {
            if (error || !std::filesystem::remove(enabled, error) || error)
            {
                return LegacyInstallMigrationResult::failed;
            }
        }
        else if (error)
        {
            return LegacyInstallMigrationResult::failed;
        }

        const auto temporary = legacy_mod / L"deprecated.txt.tmp";
        std::filesystem::remove(temporary, error);
        if (error)
        {
            return LegacyInstallMigrationResult::failed;
        }

        {
            // The migration targets UE4SS's ASCII Mods path. Using the narrow
            // form also keeps the ABI syntax check portable when it models
            // _WIN32 with a Linux standard library.
            std::ofstream stream(temporary.string(), std::ios::binary | std::ios::trunc);
            stream.write(legacy_deprecation_target.data(),
                         static_cast<std::streamsize>(legacy_deprecation_target.size()));
            stream.flush();
            if (!stream)
            {
                stream.close();
                std::filesystem::remove(temporary, error);
                return LegacyInstallMigrationResult::failed;
            }
        }

        std::filesystem::rename(temporary, marker, error);
        if (error)
        {
            std::error_code cleanup_error;
            std::filesystem::remove(temporary, cleanup_error);
            return std::filesystem::is_regular_file(marker, cleanup_error)
                ? LegacyInstallMigrationResult::already_migrated
                : LegacyInstallMigrationResult::failed;
        }
        return LegacyInstallMigrationResult::migrated;
    }
    catch (...)
    {
        return LegacyInstallMigrationResult::failed;
    }
}
}

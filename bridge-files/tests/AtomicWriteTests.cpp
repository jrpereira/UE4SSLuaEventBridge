// Order of steps in an atomic replace, against a fake file system.

#include "TestSupport.hpp"

#include <FileBridgeAtomicWrite.hpp>

#include <map>
#include <string>
#include <vector>

using namespace UE4SSLuaFileBridge::Core;
using UE4SSLuaFileBridge::NativeContract::ErrorCode;

namespace
{
class FakeOps final : public AtomicReplaceOps
{
public:
    std::map<std::string, std::string> files;
    std::vector<std::string> steps;
    std::string content = "new";
    std::string fail_step;

    Status fill_temp(const std::string& temp) override
    {
        steps.push_back("fill " + temp);
        if (fail_step == "fill")
        {
            files[temp] = "partial";
            return fail(ErrorCode::io, "disk full");
        }
        files[temp] = content;
        return Done{};
    }
    Outcome<bool> target_exists(const std::string& target) override
    {
        steps.push_back("exists " + target);
        return files.count(target) != 0;
    }
    Status replace(const std::string& target, const std::string& temp, const std::string& backup) override
    {
        steps.push_back("replace " + target + " " + temp + " " + backup);
        if (fail_step == "replace") return fail(ErrorCode::busy, "sharing violation");
        if (!backup.empty()) files[backup] = files[target];
        files[target] = files[temp];
        files.erase(temp);
        return Done{};
    }
    Status rename_new(const std::string& temp, const std::string& target) override
    {
        steps.push_back("rename " + temp + " " + target);
        if (fail_step == "rename") return fail(ErrorCode::exists, "raced");
        files[target] = files[temp];
        files.erase(temp);
        return Done{};
    }
    void discard(const std::string& temp) override
    {
        steps.push_back("discard " + temp);
        files.erase(temp);
    }
};

void names()
{
    CHECK(temp_sibling("C:/a/x.sav", 42, 7) == "C:/a/x.sav.xbtmp-42-7");
    CHECK(backup_sibling("C:/a/x.sav") == "C:/a/x.sav.bak");
}

void new_file()
{
    FakeOps ops;
    CHECK(atomic_replace(ops, "T", "T.tmp", "", true).ok());
    CHECK((ops.steps == std::vector<std::string>{"fill T.tmp", "exists T", "rename T.tmp T"}));
    CHECK(ops.files.size() == 1 && ops.files["T"] == "new");
}

void replace_existing_with_backup()
{
    FakeOps ops;
    ops.files["T"] = "old";
    CHECK(atomic_replace(ops, "T", "T.tmp", "T.bak", true).ok());
    CHECK((ops.steps == std::vector<std::string>{"fill T.tmp", "exists T", "replace T T.tmp T.bak"}));
    CHECK(ops.files["T"] == "new" && ops.files["T.bak"] == "old" && !ops.files.count("T.tmp"));
}

void refuse_existing_without_overwrite()
{
    FakeOps ops;
    ops.files["T"] = "old";
    auto status = atomic_replace(ops, "T", "T.tmp", "", false);
    CHECK(!status && status.failure().code == ErrorCode::exists);
    CHECK(ops.files["T"] == "old" && !ops.files.count("T.tmp"));
    CHECK(ops.steps.back() == "discard T.tmp");
}

void failures_leave_target_unchanged()
{
    for (const char* step : {"fill", "replace"})
    {
        FakeOps ops;
        ops.files["T"] = "old";
        ops.fail_step = step;
        auto status = atomic_replace(ops, "T", "T.tmp", "", true);
        CHECK(!status);
        CHECK(ops.files["T"] == "old");
        CHECK(!ops.files.count("T.tmp"));
        CHECK(ops.steps.back() == "discard T.tmp");
    }
    FakeOps ops;
    ops.fail_step = "rename";
    auto status = atomic_replace(ops, "T", "T.tmp", "", true);
    CHECK(!status && status.failure().code == ErrorCode::exists);
    CHECK(!ops.files.count("T") && !ops.files.count("T.tmp"));
}
}

int main()
{
    names();
    new_file();
    replace_existing_with_backup();
    refuse_existing_without_overwrite();
    failures_leave_target_unchanged();
    return FileBridgeTests::finish("AtomicWriteTests");
}

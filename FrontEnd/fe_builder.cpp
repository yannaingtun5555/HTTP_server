#include "fe_builder.hpp"
#include <iostream>
#include <filesystem>
#include <cstdio>
#include <array>
#include <stdexcept>
#include <sstream>

namespace fs = std::filesystem;

// ─────────────────────────────────────────────────────────────
//  runCommand — executes a shell command in `cwd`, captures output
// ─────────────────────────────────────────────────────────────
bool FrontendBuilder::runCommand(const std::string& cmd,
                                 const std::string& cwd,
                                 std::string& output)
{
    // Build shell command: cd <cwd> && <cmd> 2>&1
    std::string full = "cd " + cwd + " && " + cmd + " 2>&1";

    FILE* pipe = popen(full.c_str(), "r");
    if (!pipe)
    {
        output = "popen() failed for: " + full;
        return false;
    }

    std::array<char, 256> buf;
    std::ostringstream oss;
    while (fgets(buf.data(), buf.size(), pipe) != nullptr)
        oss << buf.data();

    int rc = pclose(pipe);
    output = oss.str();
    return (rc == 0);
}

// ─────────────────────────────────────────────────────────────
//  copyDir — recursively copies src → dst using std::filesystem
// ─────────────────────────────────────────────────────────────
bool FrontendBuilder::copyDir(const std::string& src,
                              const std::string& dst,
                              std::string& error_out)
{
    try
    {
        fs::create_directories(dst);
        fs::copy(src, dst,
                 fs::copy_options::recursive |
                 fs::copy_options::overwrite_existing);
        return true;
    }
    catch (const fs::filesystem_error& e)
    {
        error_out = e.what();
        return false;
    }
}

// ─────────────────────────────────────────────────────────────
//  build — main entry point
// ─────────────────────────────────────────────────────────────
bool FrontendBuilder::build(const std::string& domain,
                            const std::string& fe_folder,
                            const std::string& public_dir,
                            const std::string& fe_build,
                            std::string& error_out,
                            const Callback& cb)
{
    auto log = [&](const std::string& msg) {
        std::cout << "[FEBuilder][" << domain << "] " << msg << "\n";
    };

    log("Starting FE build (type=" + fe_build + ")");
    log("  src=" + fe_folder + "  dst=" + public_dir);

    // Validate source folder exists
    if (!fs::exists(fe_folder) || !fs::is_directory(fe_folder))
    {
        error_out = "FE source folder does not exist: " + fe_folder;
        log("ERROR: " + error_out);
        if (cb) cb(domain, false, error_out);
        return false;
    }

    fs::create_directories(public_dir);

    bool ok = false;
    std::string cmd_out;

    if (fe_build == "npm")
    {
        // npm install + npm run build — expects output in <fe_folder>/dist or build/
        ok = runCommand("npm install --silent && npm run build", fe_folder, cmd_out);
        if (!ok) { error_out = cmd_out; goto done; }

        // Copy dist/ or build/ output to public_dir
        for (auto& sub : {"dist", "build", "out", "public"})
        {
            std::string out_dir = fe_folder + "/" + sub;
            if (fs::exists(out_dir))
            {
                ok = copyDir(out_dir, public_dir, error_out);
                goto done;
            }
        }
        // Fallback: copy everything
        ok = copyDir(fe_folder, public_dir, error_out);
    }
    else if (fe_build == "vite")
    {
        ok = runCommand("npm install --silent && npx vite build", fe_folder, cmd_out);
        if (!ok) { error_out = cmd_out; goto done; }
        {
            std::string dist = fe_folder + "/dist";
            ok = copyDir(fs::exists(dist) ? dist : fe_folder, public_dir, error_out);
        }
    }
    else if (fe_build == "next")
    {
        // next export writes to <fe_folder>/out
        ok = runCommand(
            "npm install --silent && npx next build && npx next export -o out",
            fe_folder, cmd_out);
        if (!ok) { error_out = cmd_out; goto done; }
        {
            std::string out = fe_folder + "/out";
            ok = copyDir(fs::exists(out) ? out : fe_folder, public_dir, error_out);
        }
    }
    else if (fe_build == "hugo")
    {
        // Hugo writes to <fe_folder>/public by default
        ok = runCommand("hugo --minify", fe_folder, cmd_out);
        if (!ok) { error_out = cmd_out; goto done; }
        {
            std::string hugo_pub = fe_folder + "/public";
            ok = copyDir(fs::exists(hugo_pub) ? hugo_pub : fe_folder,
                         public_dir, error_out);
        }
    }
    else // "none" or unknown — bare HTML copy
    {
        // Skip if public_dir already has content (preserves manually-placed files
        // and files deployed by previous server runs / the admin panel)
        bool dst_has_files = false;
        if (fs::exists(public_dir) && fs::is_directory(public_dir))
        {
            for (auto& e : fs::directory_iterator(public_dir))
            {
                (void)e;
                dst_has_files = true;
                break;
            }
        }

        if (dst_has_files)
        {
            log("No build step — public dir already populated, skipping copy");
            ok = true;
        }
        else
        {
            log("No build step — copying files directly");
            ok = copyDir(fe_folder, public_dir, error_out);
        }
    }

done:
    if (ok)
        log("Build successful → " + public_dir);
    else
        log("Build FAILED: " + error_out);

    if (cb) cb(domain, ok, ok ? "" : error_out);
    return ok;
}

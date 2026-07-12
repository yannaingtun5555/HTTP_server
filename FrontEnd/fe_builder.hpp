#ifndef FE_BUILDER_HPP
#define FE_BUILDER_HPP

#include <string>
#include <functional>

// ─────────────────────────────────────────────────────────────
//  FrontendBuilder
//  Detects the FE build system from fe_build hint, runs the
//  appropriate tool, and copies output to the public folder.
//
//  Supported build systems (fe_build field in sites.conf):
//    npm   — runs: npm install && npm run build
//    vite  — runs: npm install && npx vite build
//    next  — runs: npm install && npx next build && npx next export
//    hugo  — runs: hugo --minify
//    none  — bare HTML: rsyncs fe_folder directly to public_dir
// ─────────────────────────────────────────────────────────────
class FrontendBuilder
{
public:
    // Callback type: (domain, success, message)
    using Callback = std::function<void(const std::string&, bool, const std::string&)>;

    // Build FE synchronously (call from a thread-pool thread)
    // fe_folder  : source directory (from sites.conf)
    // public_dir : destination (sites/<domain>/public/)
    // fe_build   : "npm" | "vite" | "next" | "hugo" | "none"
    // domain     : used in log messages / callback
    // cb         : optional completion callback
    static bool build(const std::string& domain,
                      const std::string& fe_folder,
                      const std::string& public_dir,
                      const std::string& fe_build,
                      std::string& error_out,
                      const Callback& cb = nullptr);

private:
    static bool runCommand(const std::string& cmd,
                           const std::string& cwd,
                           std::string& output);

    static bool copyDir(const std::string& src, const std::string& dst,
                        std::string& error_out);
};

#endif // FE_BUILDER_HPP

#include "vcb/Driver.hpp"
#include "vcb/Parser.hpp"
#include "vcb/Printer.hpp"
#include "vcb/X64.hpp"
#include "vcb/Pe.hpp"
#include "vcb/Elf.hpp"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace vcb {

    namespace {

        void printUsage() {
            std::fprintf(stderr,
                "usage: vcb <command> [args]\n"
                "\n"
                "Commands:\n"
                "  vcb version                                print version\n"
                "  vcb dump    <file.vcbir>                   parse and pretty-print\n"
                "  vcb build   <file.vcbir> -o <out> [--target pe|elf]\n"
                "                                             emit a native image\n"
                "  vcb headers    <file.exe>                  dump a PE header\n"
                "  vcb elfheaders <file.elf>                  dump an ELF header\n"
                "  vcb sign       <file.exe> [options]        Authenticode-sign\n"
                "  vcb verify     <file.exe>                  verify a signature\n"
                "\n"
                "Sign options:\n"
                "  --pfx <path>          .pfx certificate file\n"
                "  --password <pw>       .pfx password\n"
                "  --store <name>        certificate store (e.g. My)\n"
                "  --name <subject>      certificate subject (e.g. Vayu Dev)\n"
                "  --timestamp <url>     RFC 3161 timestamp URL\n"
                "\n"
                "Sign environment fallbacks:\n"
                "  VCB_SIGN_PFX, VCB_SIGN_PASSWORD, VCB_SIGN_STORE,\n"
                "  VCB_SIGN_NAME, VCB_TIMESTAMP_URL\n"
                "\n"
                "Build also accepts:\n"
                "  --sign                sign the output after writing\n");
        }

        int cmdVersion() {
            std::printf("vcb 0.6.0 (Phase 27 complete, PE + ELF)\n");
            return 0;
        }

        // ---- signtool integration --------------------------------------
        //
        // VCB delegates to the Windows SDK's signtool.exe.  The binary
        // lives on PATH (verified: C:\Program Files (x86)\Windows Kits\
        // 10\bin\10.0.28000.0\x64\signtool.exe).
        //
        // Signing is required for the generated image to launch on
        // Windows 11 systems where the inbox WDAC policy
        // (VerifiedAndReputableDesktop) is active.  A self-signed
        // certificate will produce a valid Authenticode signature, but
        // WDAC will not trust it -- only a certificate chained to a
        // Microsoft-trusted root will satisfy the policy.  See
        // Phase 27 Part 10 notes.
        //
        // Environment overrides (used when a flag is omitted):
        //   VCB_SIGN_PFX       path to .pfx file
        //   VCB_SIGN_PASSWORD  password for the .pfx
        //   VCB_SIGN_STORE     cert store name (e.g. "My")
        //   VCB_SIGN_NAME      cert subject common name (e.g. "Vayu Dev")
        //   VCB_TIMESTAMP_URL  RFC 3161 timestamp URL
        //                      (default: http://timestamp.digicert.com)

        std::string envOr(const char* key, const char* def) {
            const char* v = std::getenv(key);
            return (v && *v) ? std::string(v) : std::string(def);
        }

        std::string quoteArg(const std::string& s) {
            // Only quote when needed; signtool does not accept single
            // quotes on Windows.  Backslash-quoting inside double quotes
            // follows the CommandLineToArgvW rules.
            bool needs = s.empty() ||
                s.find_first_of(" \t\"") != std::string::npos;
            if (!needs) return s;
            std::string out = "\"";
            for (char c : s) {
                if (c == '"' || c == '\\') out.push_back('\\');
                out.push_back(c);
            }
            out.push_back('"');
            return out;
        }

        int runSigntool(const std::string& args) {
            std::string cmd = "signtool " + args;
            std::printf("vcb: running %s\n", cmd.c_str());
            int rc = std::system(cmd.c_str());
            if (rc == -1) {
                std::fprintf(stderr,
                    "vcb: failed to launch signtool.exe -- is it on PATH?\n");
                return 1;
            }
            return rc;
        }

        int cmdSign(const std::string& file,
            const std::string& pfx,
            const std::string& password,
            const std::string& store,
            const std::string& name,
            const std::string& tsUrl) {
            if (!std::filesystem::exists(file)) {
                std::fprintf(stderr, "vcb: no such file '%s'\n", file.c_str());
                return 1;
            }

            std::string ts = tsUrl.empty()
                ? envOr("VCB_TIMESTAMP_URL", "http://timestamp.digicert.com")
                : tsUrl;

            std::string a;
            // Common flags: SHA-256 file digest, SHA-256 timestamp digest.
            a += "sign /fd SHA256 /td SHA256 /tr " + quoteArg(ts) + " ";

            std::string pfxPath = pfx.empty()
                ? envOr("VCB_SIGN_PFX", "")
                : pfx;
            std::string pfxPw = password.empty()
                ? envOr("VCB_SIGN_PASSWORD", "")
                : password;
            std::string storeName = store.empty()
                ? envOr("VCB_SIGN_STORE", "")
                : store;
            std::string certName = name.empty()
                ? envOr("VCB_SIGN_NAME", "")
                : name;

            if (!pfxPath.empty()) {
                a += "/f " + quoteArg(pfxPath) + " ";
                if (!pfxPw.empty())
                    a += "/p " + quoteArg(pfxPw) + " ";
            }
            else if (!storeName.empty() && !certName.empty()) {
                a += "/s " + quoteArg(storeName) + " ";
                a += "/n " + quoteArg(certName) + " ";
            }
            else {
                // No explicit cert: let signtool pick the best one in the
                // current user's "My" store (the /a flag).
                a += "/a ";
            }

            a += quoteArg(file);
            return runSigntool(a);
        }

        int cmdVerify(const std::string& file) {
            if (!std::filesystem::exists(file)) {
                std::fprintf(stderr, "vcb: no such file '%s'\n", file.c_str());
                return 1;
            }
            // /pa = use the default Authenticode verification policy.
            // /v  = verbose, print signer chain.
            return runSigntool("verify /pa /v " + quoteArg(file));
        }

        int cmdDump(const std::string& path) {
            try {
                Module m = parseFile(path);
                std::string out = printModule(m);
                std::fwrite(out.data(), 1, out.size(), stdout);
                return 0;
            }
            catch (const ParseError& e) {
                if (e.line > 0)
                    std::fprintf(stderr, "%s:%d: parse error: %s\n",
                        path.c_str(), e.line, e.what());
                else
                    std::fprintf(stderr, "%s: parse error: %s\n",
                        path.c_str(), e.what());
                return 1;
            }
        }

        int cmdBuild(const std::string& input, const std::string& output,
            const std::string& target) {
            // Create the output directory if it does not exist.  Without
            // this, writePeAtomic / writeElfAtomic fail at fopen(tmp) with
            // "cannot write '<out>.tmp'" when the parent is missing.
                {
                    std::error_code ec;
                    auto parent = std::filesystem::path(output).parent_path();
                    if (!parent.empty())
                        std::filesystem::create_directories(parent, ec);
                }
                if (target != "pe" && target != "elf") {
                std::fprintf(stderr,
                    "vcb: --target '%s' not implemented yet (pe|elf)\n",
                    target.c_str());
                return 1;
            }
            try {
                Module m = parseFile(input);

                if (target == "elf") {
                    CodegenResult cg = codegenX64Elf(m);
                    ElfInputs ei;
                    ei.text = &cg.text;
                    ei.rodata = &cg.rdata;
                    ei.entryOffset = cg.entryOffset;
                    std::vector<uint8_t> image = writeElf(ei);
                    int wrc = writeElfAtomic(output, image);
                    if (wrc != 0) return 1;
                    std::printf("vcb: wrote %s (%zu bytes)\n",
                        output.c_str(), image.size());
                    return 0;
                }

                CodegenResult cg = codegenX64Pe(m);

                PeInputs pi;
                pi.text = &cg.text;
                pi.rdata = &cg.rdata;
                pi.idata = &cg.idata;
                pi.xdata = &cg.xdata;
                pi.unwindEntries = &cg.unwindEntries;
                // The image is fully PIC: every reference is RIP-relative
                // or an RVA that the loader resolves by adding the base
                // (import table entries).  There are no absolute VAs in
                // .text, so the .reloc section would legitimately be
                // empty.  Some Windows 11 builds reject images that set
                // DYNAMIC_BASE with a zero-entry .reloc, so disable ASLR
                // and set RELOCS_STRIPPED — the coherent, fixed-base
                // configuration.
                pi.enableAslr = false;
                pi.entryOffset = cg.entryOffset;
                pi.iatRva = cg.iatRva;
                pi.iatSize = cg.iatSize;
                pi.importRva = cg.importRva;
                pi.importSize = cg.importSize;

                std::vector<uint8_t> image = writePe(pi);

                int wrc = writePeAtomic(output, image);
                if (wrc != 0) return 1;
                std::printf("vcb: wrote %s (%zu bytes)\n",
                    output.c_str(), image.size());
                return 0;
            }
            catch (const ParseError& e) {
                if (e.line > 0)
                    std::fprintf(stderr, "%s:%d: parse error: %s\n",
                        input.c_str(), e.line, e.what());
                else
                    std::fprintf(stderr, "%s: parse error: %s\n",
                        input.c_str(), e.what());
                return 1;
            }
            catch (const std::exception& e) {
                std::fprintf(stderr, "vcb: %s\n", e.what());
                return 1;
            }
        }

    } // namespace

    int runDriver(int argc, char** argv) {
        if (argc < 2) { printUsage(); return 2; }
        const char* cmd = argv[1];

        if (std::strcmp(cmd, "version") == 0) return cmdVersion();

        if (std::strcmp(cmd, "dump") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: dump requires a path\n");
                return 2;
            }
            return cmdDump(argv[2]);
        }

        if (std::strcmp(cmd, "headers") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: headers requires a path\n");
                return 2;
            }
            return dumpPeHeaders(argv[2]);
        }

        if (std::strcmp(cmd, "elfheaders") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: elfheaders requires a path\n");
                return 2;
            }
            return dumpElfHeaders(argv[2]);
        }

        if (std::strcmp(cmd, "sign") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: sign requires a path\n");
                return 2;
            }
            std::string file = argv[2];
            std::string pfx, password, store, name, timestamp;
            for (int i = 3; i < argc; ++i) {
                std::string a = argv[i];
                auto next = [&](const char* what) -> std::string {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr,
                            "vcb: %s requires an argument\n", what);
                        std::exit(2);
                    }
                    return argv[++i];
                    };
                if (a == "--pfx")            pfx = next("--pfx");
                else if (a == "--password")  password = next("--password");
                else if (a == "--store")     store = next("--store");
                else if (a == "--name")      name = next("--name");
                else if (a == "--timestamp") timestamp = next("--timestamp");
                else {
                    std::fprintf(stderr,
                        "vcb: unknown sign flag '%s'\n", a.c_str());
                    return 2;
                }
            }
            return cmdSign(file, pfx, password, store, name, timestamp);
        }

        if (std::strcmp(cmd, "verify") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: verify requires a path\n");
                return 2;
            }
            return cmdVerify(argv[2]);
        }

        if (std::strcmp(cmd, "build") == 0) {
            if (argc < 3) {
                std::fprintf(stderr, "vcb: build requires a path\n");
                return 2;
            }
            std::string input;
            std::string output;
            std::string target = "pe";
            bool signAfter = false;
            for (int i = 2; i < argc; ++i) {
                std::string a = argv[i];
                if (a == "-o") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr, "vcb: -o requires a path\n");
                        return 2;
                    }
                    output = argv[++i];
                }
                else if (a == "--target") {
                    if (i + 1 >= argc) {
                        std::fprintf(stderr, "vcb: --target requires an argument\n");
                        return 2;
                    }
                    target = argv[++i];
                }
                else if (a == "--sign") {
                    signAfter = true;
                }
                else if (input.empty()) {
                    input = a;
                }
                else {
                    std::fprintf(stderr, "vcb: unexpected argument '%s'\n", a.c_str());
                    return 2;
                }
            }
            if (input.empty() || output.empty()) {
                std::fprintf(stderr,
                    "vcb: build requires <input> and -o <output>\n");
                return 2;
            }
            int rc = cmdBuild(input, output, target);
            if (rc != 0) return rc;
            if (signAfter) {
                // Sign with whatever cert the environment or user store
                // provides.  If nothing is configured, signtool's /a
                // will pick the best available cert from the current
                // user's "My" store, or fail with a clear message.
                int src = cmdSign(output, "", "", "", "", "");
                if (src != 0) {
                    std::fprintf(stderr,
                        "vcb: build succeeded but signing failed; the "
                        "unsigned binary is at %s\n", output.c_str());
                    return src;
                }
            }
            return 0;
        }

        std::fprintf(stderr, "vcb: unknown command '%s'\n", cmd);
        printUsage();
        return 2;
    }

} // namespace vcb
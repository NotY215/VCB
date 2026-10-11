#include "vcb/Policy.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

namespace vcb {

    namespace {

        int writeFile(const std::string& path, const std::string& body) {
            std::error_code ec;
            auto parent = std::filesystem::path(path).parent_path();
            if (!parent.empty())
                std::filesystem::create_directories(parent, ec);
            std::ofstream out(path, std::ios::binary);
            if (!out) {
                std::fprintf(stderr,
                    "vcb: cannot write '%s'\n", path.c_str());
                return 1;
            }
            out << body;
            std::fprintf(stderr, "vcb: wrote %s\n", path.c_str());
            return 0;
        }

        std::string joinPath(const std::string& dir, const char* name) {
            if (dir.empty()) return name;
            std::string p = dir;
            if (p.back() != '/' && p.back() != '\\') {
#ifdef _WIN32
                p += '\\';
#else
                p += '/';
#endif
            }
            p += name;
            return p;
        }

    } // namespace

    // ---- WDAC ------------------------------------------------------------

    static const char* kWdacXml = R"xml(<?xml version="1.0" encoding="utf-8"?>
<!--
  Vayu / VCB WDAC supplemental policy template.

  Purpose:  allow executables produced by vcb 0.6.x to run on a host
  where the WDAC inbox policy is active.  Because VCB does not sign
  its output, this template uses HASH rules.

  Deploy:
    1. Build your Vayu program with vcb.
    2. certutil -hashfile myprog.exe SHA256
    3. Paste the hex digest (no spaces, uppercase) into an <Allow>
       entry below.
    4. ConvertFrom-CIPolicy vayu-wdac-supplemental.xml ^
                         vayu-wdac-supplemental.p7b
    5. Deploy the .p7b as a supplemental policy.

  This is NOT a bypass.  It is the supported mechanism for an
  administrator to allow specific unsigned binaries on a WDAC host.
  Only apply it on a machine you own and administer.
-->
<SiPolicy xmlns="urn:schemas-microsoft-com:sipolicy"
          PolicyType="Supplemental Policy">
  <VersionEx>10.0.0.0</VersionEx>
  <PolicyID>{00000000-0000-0000-0000-000000000000}</PolicyID>
  <BasePolicyID>{00000000-0000-0000-0000-000000000000}</BasePolicyID>
  <Rules>
    <Rule><Option>Enabled:Unsigned System Integrity Policy</Option></Rule>
    <Rule><Option>Enabled:Update Policy No Reboot</Option></Rule>
  </Rules>
  <FileRules>
    <!-- One <Allow Hash="..."/> per binary you want to allow. -->
    <Allow Hash="0000000000000000000000000000000000000000000000000000000000000000"/>
  </FileRules>
  <Signers>
    <Signer ID="ID_SIGNER_VCB_HASH" Name="VCB Hash Signer">
      <CertRoot Type="TBS" Value="0000000000000000000000000000000000000000000000000000000000000000"/>
    </Signer>
  </Signers>
  <SigningScenarios>
    <SigningScenario Value="131" ID="ID_SIGNINGSCENARIO_WINDOWS">
      <ProductSigners>
        <FileRulesRef>
          <FileRuleRef RuleID="ID_SIGNER_VCB_HASH"/>
        </FileRulesRef>
      </ProductSigners>
    </SigningScenario>
    <SigningScenario Value="12" ID="ID_SIGNINGSCENARIO_KMCI">
      <ProductSigners/>
    </SigningScenario>
  </SigningScenarios>
</SiPolicy>
)xml";

    int writeWdacTemplate(const std::string& dir) {
        return writeFile(joinPath(dir, "vayu-wdac-supplemental.xml"),
                         kWdacXml);
    }

    // ---- SELinux ---------------------------------------------------------

    static const char* kSelinuxTe = R"te(#
# Vayu / VCB SELinux policy module template.
#
# Install (as root):
#   checkmodule -M -m -o vayu.mod vayu.te
#   semodule_package -o vayu.pp -m vayu.mod -f vayu.fc
#   semodule -i vayu.pp
#
# This is NOT a bypass of SELinux.  It is a normal, reviewable
# policy module.

policy_module(vayu, 1.0.0)

require {
    type bin_t;
    class file { execute execute_no_trans open read };
    class dir { search read };
}

type vayu_t;
type vayu_exec_t;

init_daemon_domain(vayu_t, vayu_exec_t)

allow vayu_t bin_t:file { execute execute_no_trans open read };
allow vayu_t self:process { execstack execmem };
allow vayu_t self:file   { read write };
allow vayu_t self:fifo_file write;
)te";

    static const char* kSelinuxFc = R"fc(#
# File-context template:  label VCB-built binaries with vayu_exec_t.
# Adjust the regular expression to suit your deployment.

/usr/local/bin/vayu-[^/]*    --    gen_context(system_u:object_r:vayu_exec_t,s0)
)fc";

    int writeSelinuxTemplate(const std::string& dir) {
        int rc = writeFile(joinPath(dir, "vayu.te"), kSelinuxTe);
        if (rc != 0) return rc;
        return writeFile(joinPath(dir, "vayu.fc"), kSelinuxFc);
    }

    // ---- AppArmor --------------------------------------------------------

    static const char* kApparmorProfile = R"aa(#
# Vayu / VCB AppArmor profile template.
#
# Install (as root):
#   cp vayu.profile /etc/apparmor.d/usr.local.bin.vayu
#   apparmor_parser -r /etc/apparmor.d/usr.local.bin.vayu
#
# This is NOT a bypass of AppArmor.  It is a normal, reviewable
# profile.

#include <tunables/global>

profile vayu /usr/local/bin/vayu-* flags=(attach_disconnected) {
  #include <abstractions/base>

  /usr/local/bin/vayu-*  mr,
  /dev/tty      rw,
  /dev/stdout   rw,
  /dev/stderr   rw,

  # List any files your program opens explicitly.
  # Never use /** rw -- that defeats the profile.
}
)aa";

    int writeApparmorTemplate(const std::string& dir) {
        return writeFile(joinPath(dir, "vayu.profile"), kApparmorProfile);
    }

    // ---- IMA -------------------------------------------------------------

    static const char* kImaPolicy = R"(#
# Vayu / VCB IMA appraisal policy template.
#
# Apply (as root):
#   for f in /usr/local/bin/vayu-*; do
#       evmctl ima_sign --hashalgo sha256 "$f"
#   done
#   cat vayu-ima-policy.txt >> /etc/ima/ima-policy
#
# This is NOT a bypass of IMA.  It is the standard way to add a new
# signer or a new file to IMA's appraise list.

measure func=FILE_CHECK mask=MAY_EXEC  fowner=0 uid=0
appraise func=FILE_CHECK mask=MAY_EXEC  fowner=0 uid=0
appraise func=BPRM_CHECK mask=MAY_EXEC
appraise fowner=0 obj_type=vayu_exec_t
)";

    int writeImaTemplate(const std::string& dir) {
        return writeFile(joinPath(dir, "vayu-ima-policy.txt"), kImaPolicy);
    }

    // ---- Dispatcher ------------------------------------------------------

    int runPolicy(const std::string& name, const std::string& dir) {
        if (name == "wdac")     return writeWdacTemplate(dir);
        if (name == "selinux")  return writeSelinuxTemplate(dir);
        if (name == "apparmor") return writeApparmorTemplate(dir);
        if (name == "ima")      return writeImaTemplate(dir);
        if (name == "all") {
            int rc = 0;
            if (writeWdacTemplate(dir))     rc = 1;
            if (writeSelinuxTemplate(dir))  rc = 1;
            if (writeApparmorTemplate(dir)) rc = 1;
            if (writeImaTemplate(dir))      rc = 1;
            return rc;
        }
        std::fprintf(stderr,
            "vcb: policy '%s' not recognized "
            "(wdac|selinux|apparmor|ima|all)\n", name.c_str());
        return 1;
    }

} // namespace vcb
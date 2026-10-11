#pragma once
#include <string>

namespace vcb {

    // Phase 29 -- MAC policy template emitters.
    //
    // These functions do not bypass any MAC system.  They produce the
    // configuration a system administrator needs in order to allow
    // VCB-built binaries to execute on a host with the named policy
    // active.  Every template carries a header explaining what it does
    // and what privileges it grants.
    //
    //   wdac      Windows Defender Application Control supplemental
    //             policy (XML), hash-based allow rules.
    //   selinux   SELinux policy module (.te + .fc).
    //   apparmor  AppArmor profile.
    //   ima       IMA appraisal policy snippet.
    //
    // `dir` may be empty, in which case files are written to the
    // current directory.

    int writeWdacTemplate(const std::string& dir);
    int writeSelinuxTemplate(const std::string& dir);
    int writeApparmorTemplate(const std::string& dir);
    int writeImaTemplate(const std::string& dir);

    // Dispatcher for `vcb policy <name>`.  `name == "all"` writes
    // every template.  Returns 0 on success, 1 on unknown name.
    int runPolicy(const std::string& name, const std::string& dir);

} // namespace vcb
import subprocess
import filecmp, tempfile, shutil, os

# Thank you https://docs.platformio.org/en/latest/projectconf/section_env_build.html !

gitFail = False
try:
    subprocess.check_call(["git", "status"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
except:
    gitFail = True

if gitFail:
    # Provide default version info if git is not available
    tag = "v4.0.0"  # Using semantic version format
    rev = " (local)" 
    url = "https://git.fablabkerala.in/pub/fab-projects-pub/ssk-mtm-academic-internship"
else:
    try:
        # Try to get tag, but provide default if none exists
        try:
            tag = subprocess.check_output(["git", "describe", "--tags", "--abbrev=0"], 
                                        stderr=subprocess.DEVNULL).strip().decode("utf-8")
        except:
            tag = "v4.0.0"  # Default version if no tags exist
            
        # Get branch and commit info
        branchname = subprocess.check_output(["git", "rev-parse", "--abbrev-ref", "HEAD"]).strip().decode("utf-8")
        revision = subprocess.check_output(["git", "rev-parse", "--short", "HEAD"]).strip().decode("utf-8")
        
        # Check for local changes
        modified = subprocess.check_output(["git", "status", "-uno", "-s"]).strip().decode("utf-8")
        dirty = "-dirty" if modified else ""
        
        rev = f" ({branchname}-{revision}{dirty})"
        
        # Get remote URL or use default
        try:
            url = subprocess.check_output(["git", "config", "--get", "remote.origin.url"]).strip().decode("utf-8")
        except:
            url = "https://git.fablabkerala.in/pub/fab-projects-pub/ssk-mtm-academic-internship"
            
    except Exception as e:
        print(f"Git command failed: {str(e)}")
        # Fallback values
        tag = "v4.0.0"
        rev = " (error)"
        url = "https://git.fablabkerala.in/pub/fab-projects-pub/ssk-mtm-academic-internship"

grbl_version = tag.replace('v','').rpartition('.')[0]
git_info = '%s%s' % (tag, rev)
git_url = url

provisional = "FluidNC/src/version.cxx"
final = "FluidNC/src/version.cpp"
with open(provisional, "w") as fp:
    fp.write('const char* grbl_version = \"' + grbl_version + '\";\n')
    fp.write('const char* git_info     = \"' + git_info + '\";\n')
    fp.write('const char* git_url      = \"' + git_url + '\";\n')

if not os.path.exists(final):
    # No version.cpp so rename version.cxx to version.cpp
    os.rename(provisional, final)
elif not filecmp.cmp(provisional, final):
    # version.cxx differs from version.cpp so get rid of the
    # old .cpp and rename .cxx to .cpp
    os.remove(final)
    os.rename(provisional, final)
else:
    # The existing version.cpp is the same as the new version.cxx
    # so we can just leave the old version.cpp in place and get
    # rid of version.cxx
    os.remove(provisional)

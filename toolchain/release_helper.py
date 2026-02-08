
import json
import subprocess
import re
import os
import sys

def get_version_from_config():
    try:
        if os.path.exists('config.toml'):
            with open('config.toml', 'r') as f:
                content = f.read()
            # Look for version = "1.2.3" under [project]
            # We assume [project] is at the top or we just look for version = "..."
            m = re.search(r'version\s*=\s*"([^"]+)"', content)
            if m:
                return m.group(1)
    except Exception as e:
        print(f"Error reading config.toml: {e}")
    return None

def parse_version(v_str):
    # expect v1.2.3 or 1.2.3, returns tuple (1, 2, 3)
    m = re.search(r'(\d+)\.(\d+)\.(\d+)', v_str)
    if m:
        return [int(x) for x in m.groups()]
    return [0, 0, 0]

def version_to_int(v_parts):
    # Weight the parts to allow comparison
    # Major * 10000 + Minor * 100 + Patch
    # This assumes Patch < 100, Minor < 100 which is reasonable for this logic
    # "Minor version incremented by 2" -> 
    # If the user means the 'Minor' field (index 1), a diff of 2 is 200 units.
    # If the user means the 'Patch' field (index 2), a diff of 2 is 2 units.
    # We will use the raw integer value to catch any increment of 2 'steps' in the smallest unit.
    return v_parts[0] * 10000 + v_parts[1] * 100 + v_parts[2]

def run_git_cmd(args):
    try:
        return subprocess.check_output(args, text=True).strip()
    except subprocess.CalledProcessError:
        return ""

def main():
    if 'GITHUB_OUTPUT' not in os.environ:
        print("Not running in GitHub Actions environment (missing GITHUB_OUTPUT).")
        # Just for local testing, print to stdout
        output_file = "/dev/stdout"
    else:
        output_file = os.environ['GITHUB_OUTPUT']

    current_v_str = get_version_from_config()
    if not current_v_str:
        print("Could not find version in config.toml")
        sys.exit(1)

    print(f"Current version based on file: {current_v_str}")
    
    # Get last tag
    # Need to ensure tags are fetched. The workflow must do fetch-depth: 0
    tags_output = run_git_cmd(["git", "tag", "--sort=-v:refname"])
    tags = [t for t in tags_output.split('\n') if t.startswith('v')]
    
    last_tag = tags[0] if tags else "v0.0.0"
    print(f"Last release tag: {last_tag}")

    # Special case: If current version is same as last tag, we definitely don't release.
    if current_v_str == last_tag:
        print("Current version matches last tag. No release.")
        with open(output_file, 'a') as fh:
            fh.write("should_release=false\n")
        return

    curr_parts = parse_version(current_v_str)
    last_parts = parse_version(last_tag)
    
    curr_major, curr_minor, curr_patch = curr_parts
    last_major, last_minor, last_patch = last_parts
    
    print(f"Comparison: Current({curr_major}.{curr_minor}.{curr_patch}) vs Last({last_major}.{last_minor}.{last_patch})")

    should_release = False
    fail_reason = "Condition not met"

    should_tag_only = False

    if curr_major > last_major:
        fail_reason = "Major version incremented (Manual release required)"
        # Should we tag major versions automatically? User said "create tags for each minor".
        # Assuming we tag any version bump that isn't a Release.
        should_tag_only = False 
    elif curr_minor - last_minor >= 2:
        should_release = True
    elif curr_minor - last_minor >= 1:
        # User wants "tags for each minor" but release only if gap >= 2
        should_tag_only = True
        fail_reason = "Minor increment < 2, tagging only"
    else:
        fail_reason = f"Minor increment ({curr_minor - last_minor}) is less than 1"

    # Get Repo URL for linking
    repo_url = os.environ.get('GITHUB_SERVER_URL', 'https://github.com') + "/" + os.environ.get('GITHUB_REPOSITORY', 'example/repo')
    
    if should_release:
        print("Condition met: Release triggers.")
        
        # Generate changelog
        log_range = f"{last_tag}..HEAD" if last_tag != "v0.0.0" else "HEAD"
        
        # Format: * hash - Message (Author)
        log_cmd = ["git", "log", log_range, "--pretty=format:* %h - %s (%an)"]
        raw_log = run_git_cmd(log_cmd)
        
        # Process log for links
        # 1. Link hashes: [hash](url/commit/hash)
        # Using a simplistic approach assuming 7-char hash at start
        processed_lines = []
        for line in raw_log.split('\n'):
            # Linkify hash at start
            line = re.sub(r'^\* ([0-9a-f]+)', f'* [`\\1`]({repo_url}/commit/\\1)', line)
            # Linkify Issues/PRs (#123)
            line = re.sub(r'(#(\d+))', f'[`\\1`]({repo_url}/issues/\\2)', line)
            processed_lines.append(line)
            
        changelog = "\n".join(processed_lines)
        
        # Prepend header and Append Warnings/Footer
        full_changelog = f"## Changes since {last_tag}\n\n{changelog}\n\n"
        full_changelog += "---\n"
        full_changelog += "### ⚠️ IMPORTANT\n"
        full_changelog += "**Please BACKUP your calibration data before flashing this firmware!**\n\n"
        full_changelog += "<sub>This software is licensed under the GNU GPL v3.</sub>"

        with open(output_file, 'a') as fh:
            fh.write("should_release=true\n")
            fh.write("should_tag_only=false\n")
            fh.write(f"release_tag=v{current_v_str}\n")
            
            # Multiline string handling
            delimiter = "EOF"
            fh.write(f"changelog<<{delimiter}\n")
            fh.write(full_changelog)
            fh.write(f"\n{delimiter}\n")

    elif should_tag_only:
        print(f"Tagging only: {fail_reason}")
        with open(output_file, 'a') as fh:
            fh.write("should_release=false\n")
            fh.write("should_tag_only=true\n")
            fh.write(f"release_tag=v{current_v_str}\n")

    else:
        print(f"Skipping release/tag: {fail_reason}")
        with open(output_file, 'a') as fh:
            fh.write("should_release=false\n")
            fh.write("should_tag_only=false\n")

if __name__ == "__main__":
    main()

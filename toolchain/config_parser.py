#!/usr/bin/env python3
import sys
import tomli

def get_keys(d, keys):
    for key in keys:
        if key in d:
            d = d[key]
        else:
            return None
    return d

def main():
    if len(sys.argv) < 3:
        print("Usage: config_parser.py <config_file> <command> [args...]", file=sys.stderr)
        sys.exit(1)

    config_file = sys.argv[1]
    command = sys.argv[2]

    try:
        with open(config_file, "rb") as f:
            config = tomli.load(f)
    except Exception as e:
        print(f"Error loading config: {e}", file=sys.stderr)
        sys.exit(1)

    if command == "get_project_info":
        project = config.get("project", {})
        version = project.get("version", "0.0.0")
        name = project.get("name", "deltafw")
        authors = ", ".join(project.get("authors", ["qshosfw"]))
        print(f'VERSION="{version}"')
        print(f'PROJECT_NAME="{name}"')
        print(f'AUTHORS="{authors}"')
        
    elif command == "get_version":
        project = config.get("project", {})
        version = project.get("version", "0.0.0")
        print(version)

    elif command == "get_authors":
        project = config.get("project", {})
        authors = ", ".join(project.get("authors", ["qshosfw"]))
        print(authors)

    elif command == "get_name":
        project = config.get("project", {})
        name = project.get("name", "deltafw")
        print(name)
        
    elif command == "get_preset_args":
        if len(sys.argv) < 4:
            print("Usage: get_preset_args <preset_name>", file=sys.stderr)
            sys.exit(1)
            
        preset_name = sys.argv[3]
        
        # 1. Load Defaults
        defaults = config.get("defaults", {})
        
        # 2. Load Preset Options
        preset = config.get("presets", {}).get(preset_name)
        if not preset:
            print(f"Error: Preset '{preset_name}' not found.", file=sys.stderr)
            sys.exit(1)
            
        preset_options = preset.get("options", {})
        
        # 3. Merge (Preset overrides Defaults)
        # Create a new dict starting with defaults
        final_options = defaults.copy()
        # Update with preset options
        final_options.update(preset_options)
        
        # 4. Generate Args
        args = []
        for key, value in final_options.items():
            if isinstance(value, bool):
                val_str = "true" if value else "false"
            else:
                val_str = str(value)
            
            # Escape strings if needed (simple check)
            if isinstance(value, str) and " " in value:
                 val_str = f"'{val_str}'"
                 
            args.append(f"-D{key}={val_str}")
            
        print(" ".join(args))

    elif command == "list_presets":
        presets = config.get("presets", {}).keys()
        print(" ".join(presets))
        
    else:
        print(f"Unknown command: {command}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()

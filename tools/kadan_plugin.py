"""Names and scoped migration for an explicitly selected Kadan project."""
import json
from pathlib import Path

NAME = 'Kadan'
LEGACY_NAME = 'P4MapKitAuthoring'


def plugin_slot(project, migrate=False):
    """Ordinary builds refuse legacy slots; migration requires an explicit opt-in."""
    if migrate:
        return prepare_plugin(project)
    project = Path(project)
    settings = json.loads(project.read_text(encoding='utf-8-sig'))
    if (project.parent / 'Plugins' / LEGACY_NAME).exists() or any(row.get('Name') == LEGACY_NAME for row in settings.get('Plugins', [])):
        raise ValueError('This project uses the legacy plugin name. Select --migrate-plugin-name to update this project to Kadan.')
    return project.parent / 'Plugins' / NAME


def prepare_plugin(project):
    """Move only this project's known legacy plugin slot and enable its new name.

    Content and unrelated project/plugin settings stay untouched. The old
    descriptor's module entries stay intact until the caller stages or installs
    a new build, allowing the development builder's existing failure restoration.
    """
    project = Path(project).absolute()
    if project.suffix.lower() != '.uproject' or not project.is_file():
        raise ValueError('Choose an existing Unreal project')
    folder = project.parent.resolve()
    plugins = folder / 'Plugins'
    old, new = plugins / LEGACY_NAME, plugins / NAME
    for path in (plugins, old, new):
        if path.is_symlink() or path.resolve() != path.absolute() or not path.resolve().is_relative_to(folder):
            raise ValueError('The selected plugin slot is redirected')
    if old.exists() and new.exists():
        raise ValueError('Both plugin names are installed in this project. Resolve the duplicate before updating Kadan.')
    settings = json.loads(project.read_text(encoding='utf-8-sig'))
    rows = settings.setdefault('Plugins', [])
    matching = [row for row in rows if row.get('Name') in (NAME, LEGACY_NAME)]
    if len(matching) > 1:
        raise ValueError('The project has duplicate Kadan plugin settings')
    if old.exists():
        descriptor = old / (LEGACY_NAME + '.uplugin')
        if not descriptor.is_file():
            raise ValueError('The legacy plugin has no recognized descriptor')
        names = [row.get('Name') for row in json.loads(descriptor.read_text(encoding='utf-8'))['Modules']]
        if LEGACY_NAME not in names or len(names) != len(set(names)) or not set(names) <= {LEGACY_NAME, 'P4BlueprintImport', 'P4MapKitTests'}:
            raise ValueError('The legacy plugin contains other modules and was preserved')
        old_source = old / 'Source' / LEGACY_NAME
        if old_source.is_symlink() or old_source.resolve() != old_source.absolute():
            raise ValueError('The legacy module source is redirected')
        old.rename(new)
        (new / (LEGACY_NAME + '.uplugin')).rename(new / (NAME + '.uplugin'))
        old_source = new / 'Source' / LEGACY_NAME
        if old_source.is_dir():
            source = new / 'Source' / NAME
            old_source.rename(source)
            rules = source / (LEGACY_NAME + '.Build.cs')
            if rules.is_file():
                rules.rename(source / (NAME + '.Build.cs'))
    if matching:
        matching[0]['Name'] = NAME
    else:
        rows.append(dict(Name=NAME, Enabled=True))
    encoded = (json.dumps(settings, indent=2) + '\n').encode('utf-8')
    if project.read_bytes() != encoded:
        project.write_bytes(encoded)
    return new

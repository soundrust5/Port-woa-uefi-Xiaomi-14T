"""Resolve Linux Python 3.14 wheels using Windows networking for offline WSL pip.

pip's --platform selects wheel tags but does not change environment markers.
The marker override is confined to this downloader process and models the
actual WSL interpreter. Nothing changes in installed package source files.
"""
from pathlib import Path
import sys

from pip._vendor.packaging import markers
from pip._internal.cli.main import main

ROOT = Path(__file__).resolve().parents[1]
original_environment = markers.default_environment


def linux_environment():
    environment = original_environment()
    environment.update(os_name='posix', sys_platform='linux', platform_system='Linux',
                       platform_machine='x86_64', python_version='3.14',
                       python_full_version='3.14.4', implementation_version='3.14.4')
    return environment


markers.default_environment = linux_environment
args = ['download', '--dest', str(ROOT / 'tools/vendor/mu-wheels'),
        '--python-version', '314', '--implementation', 'cp', '--abi', 'cp314', '--abi', 'abi3',
        '--only-binary=:all:', '-r', str(ROOT / 'mu/pip-requirements.txt')]
for platform in ['manylinux2014_x86_64', 'manylinux_2_17_x86_64',
                 'manylinux_2_28_x86_64', 'manylinux_2_34_x86_64']:
    args += ['--platform', platform]
sys.exit(main(args))

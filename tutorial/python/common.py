#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

###############################################################################################
# common.py: Common helper functions
#
###############################################################################################

import shutil
import platform
import os
import sys
from pathlib import Path
from git import Repo
from packaging.version import Version as packaging_version
from packaging.specifiers import SpecifierSet as packaging_specifier
from importlib.metadata import version as metadata_version

# Prerequisite/version checks, exit in case of a mismatch...
def validate_setup():
  # The example assumes a minimal Python version and an exact Harvesters package version.
  # Because Harvesters is currently under active development, switching to different version might require
  # changes to this script. Note also that Harvesters itself might imply additional restrictions
  # on supported Python version - please consult Harvesters documentation if required.
  MIN_PYTHON_VER = (3, 6)
  if sys.version_info < MIN_PYTHON_VER:
    sys.exit("Minimal required Python version for this script is {}.{}, your version is {}".format(*MIN_PYTHON_VER, sys.version))
  supported_versions = packaging_specifier("~=1.4.0")
  if packaging_version(metadata_version('harvesters')) not in supported_versions:
    sys.exit("Required Harvesters version for this script is {}, your version is {}".format(
        supported_versions, metadata_version('harvesters')))
    
# Helper to get the directory where we want to clone the repository containing the cti file to
def get_cti_parent():
  if getattr(sys, 'frozen', False):
    # Running inside a PyInstaller bundle
    cti_parent = sys._MEIPASS
  else:
    # Running in normal Python environment
    cti_parents = Path(__file__).resolve().parents

  if len(cti_parents) < 2:
    sys.exit("Please run the script with the original repository directory structure. The script expects to be run from the 'tutorial/python' directory, with the '.cti' directory in the parent directory.")

  return cti_parents[2]

# Helper to get cti file path corresponding with the platform the script is running on
def get_cti_path():
  cti_platform_dir_name = get_cti_dir_name()
  cti_parent = get_cti_parent()
  CTI_FILENAME = "SICKGigEVisionTL.cti"
  return os.path.join(cti_parent, ".cti", cti_platform_dir_name, CTI_FILENAME)

# Helper to get cti file directory name corresponding with the platform the script is running on
def get_cti_dir_name():
  if platform.system() == "Windows":
    return "windows_x64/bin/sick_gigevision_tl"
  if platform.system() == "Linux" and platform.machine() == "x86_64":
    return "linux_x64/lib/sick_gigevision_tl"
  if platform.system() == "Linux" and platform.machine() == "aarch64":
    return "linux_aarch64/lib/sick_gigevision_tl"

  # Not one of our recognized platforms, cti file not available
  sys.exit("GenTL Producer not available on this platform")

# Helper to clone the repository containing the cti file
# usage is restricted according to EULA see sick_visionary_gev_base
def download_cti_file(remove_if_exists=False):
  repo_url = "https://github.com/SICKAG/sick_visionary_gev_base.git"
  branch = "1.0.0"
  clone_dir = os.path.join(get_cti_parent(), ".cti")

  if os.path.exists(clone_dir):
    if remove_if_exists:
      shutil.rmtree(clone_dir)
      print(f"Removed existing directory: {clone_dir}")
    else:
      return

  # Clone the repo
  Repo.clone_from(repo_url, clone_dir, branch=branch)
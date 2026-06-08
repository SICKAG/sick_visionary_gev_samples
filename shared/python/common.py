#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

###############################################################################################
# common.py: Common helper functions
#
###############################################################################################

import os
import platform
import re
import shutil
import sys
from argparse import Action, ArgumentParser, ArgumentTypeError
from argparse import RawDescriptionHelpFormatter as rdhf
from importlib.metadata import version as metadata_version
from logging import INFO, basicConfig, debug, error, info
from pathlib import Path
from time import time

import cv2
import genicam.genapi as GenApi
import numpy as np
from git import Repo
from harvesters.core import Harvester
from packaging.specifiers import SpecifierSet as packaging_specifier
from packaging.version import Version as packaging_version
from toml import loads

if list(map(int, cv2.__version__.split('.')))[0] > 3:
    from cv2 import COLOR_BAYER_RG2RGB_EA as BayerPattern
    from cv2 import cvtColor
else:
    from cv2 import cvtColor, COLOR_BayerRGGB2BGR_EA as BayerPattern

from gev_helper import apply_param, set_components

DEVICE_ACCESS_STATUS_READWRITE = 1  # GenICam/GenTL dfinition
# relaxed, better stability for bad network bandwidth
FETCH_TIMEOUT = 3.0
CONFIG_NUM_PREFIX_SIZE = 6


class ListArgs(Action):
    def __call__(self, parser, namespace, values, option_string=None):
        args = list(map(int, values.split(',')))
        if len(args) != 3:
            raise ArgumentTypeError("Must define 3 comma-separated values: "
                                    "<start>,<end>,<step>")
        args = range(args[0], args[1]+1, args[2])
        if args.start > args.stop:
            raise AttributeError("<start> must be smaller than <end>")
        setattr(namespace, self.dest, args)

# Prerequisite/version checks, exit in case of a mismatch...


def validate_setup():
    # The example assumes a minimal Python version and an exact Harvesters package version.
    # Because Harvesters is currently under active development, switching to different version might require
    # changes to this script. Note also that Harvesters itself might imply additional restrictions
    # on supported Python version - please consult Harvesters documentation if required.
    MIN_PYTHON_VER = (3, 6)
    if sys.version_info < MIN_PYTHON_VER:
        sys.exit("Minimal required Python version for this script is {}.{}, your version is {}".format(
            *MIN_PYTHON_VER, sys.version))
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
    repo_url = "https://gitlab.sickcn.net/bcap/bu-mobile-perception/systemintegration/sick_visionary_gev_base.git"
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


def init_harvester():
    try:
        h = Harvester()
        cti = get_cti_path()
        h.add_file(cti, check_existence=True, check_validity=True)
        info("CTI driver loaded...")
        h.update()
        info(
            f"Device discovery done, received {len(h.device_info_list)} answers")
        return h
    except Exception as err:
        error(f"No devices found: {err}")
        exit(1)


def select_devices(device_list, config):
    serials = str(config['cameras']['serial'])
    device_ids = list()
    info("Accessible cameras with matching serial:")
    for idx, device in enumerate(device_list):
        acc_stat = device.access_status
        if acc_stat == DEVICE_ACCESS_STATUS_READWRITE and device.serial_number in serials:
            device_ids.append(idx)
            info(f"  {device.display_name} ({device.serial_number})")
    return device_ids


def parse_config(config_file):
    with open(config_file, 'r', encoding='utf-8') as infile:
        # If parsing in the "[gev_param]" section prefix CONFIG_NUM_PREFIX_SIZE digit
        # line number. Only if line begins with alphanumerical letters. This make it work
        # with duplicated keys, line number is removed in config_camera()
        lines = infile.readlines()
        do_prefix = False
        for i, line in enumerate(lines):
            if "[gev_params]" in line:
                do_prefix = True
            elif do_prefix and re.match(r'^\s*\[.*\]', line):
                do_prefix = False
            elif re.match(r'^\s*[A-Za-z0-9]{3}', line) and do_prefix:
                lines[i] = f'{i:0{CONFIG_NUM_PREFIX_SIZE}}{lines[i]}'
        config = loads("".join(lines))
        return config


def setup_camera_objects(harvester, device_ids):
    cameras = list()
    info("Cameras to be used:")
    for idx in device_ids:
        device = harvester.device_info_list[idx]
        info(f"  {device.display_name} ({device.serial_number})")
        cameras.append(setup_camera_object(harvester, idx))
    return cameras


def setup_camera_object(harvester, device_idx):
    ia = harvester.create(device_idx)
    ia.num_buffers = 10
    ia.stop()
    device = harvester.device_info_list[device_idx]
    camera = {
        'name': f"{device.display_name}_{device.serial_number}",
        'ia': ia,
        'nm': ia.remote_device.node_map,
        'writer': None,  # init later after params config
        'frameCount': 0,
        'recordedCount': 0,
    }
    return camera


def config_camera(camera, config, skip=True):
    """
    Applies config items to a camera

    Parameters
    camera : camera object (dict) to apply the configuration
            must provide 'nm' (node map) and 'name'
    config : config object from reading a toml file
    skip : allow to skip non writable features
    """
    for name, val in config['gev_params'].items():
        line_number = int(name[:CONFIG_NUM_PREFIX_SIZE])
        feature_name = name[CONFIG_NUM_PREFIX_SIZE:]
        debug(f'  Config from file line: {line_number}')
        cmd = f'GenApi.is_writable(camera[\'nm\'].{feature_name})'
        writable = eval(cmd)
        if not writable and skip:
            info(f'  Skip {feature_name} as it is not writable')
        else:
            cmd = f'isinstance(camera[\'nm\'].{feature_name}, GenApi.IEnumeration)'
            is_enum = eval(cmd)
            if is_enum:
                cmd = f'camera[\'nm\'].{feature_name}.symbolics'
                enum_entries = eval(cmd)
                if val not in enum_entries:
                    info(
                        f'  Skip {feature_name} as {val} is not available in this device')
                    continue
            apply_param(camera['nm'], feature_name, val)
            info(f'  Set {feature_name}={val}')
    available_components = camera['nm'].ComponentSelector.symbolics
    for comp in config['gev_config']['ComponentList']:
        if comp in available_components:
            info(f'  Set Component \'{comp}\': ComponentEnable = True')
        else:
            info(
                f'  Skip component {comp} as it is not available in this device')
            config['gev_config']['ComponentList'].remove(comp)
    set_components(camera['nm'], config['gev_config']['ComponentList'])
    info(f"Applied configuration for camera: {camera['name']}")


def doFetch(camera, config):
    with camera['ia'].fetch(timeout=FETCH_TIMEOUT) as buffer:
        camera['frameCount'] += 1
        if camera['frameCount'] % config['cameras']['recordingRate'] == 0:
            camera['recordedCount'] += 1
            camera['writer'].store(buffer, camera['nm'])


def maybe_capture_secs(cameras, config, t_start, duration):
    if not duration:
        return
    if len(cameras) == 1:
        info(f"Capture frames for {duration} seconds")
        cam = cameras[0]
        cam['ia'].start()
        while time() < (t_start + duration):
            doFetch(cam, config)
        cam['ia'].stop()
    else:
        # cameras list==0 handled outside/before this function
        info(
            f"Capture frames (round-robin for multiple cameras) for {duration} seconds")
        while time() < (t_start + duration):
            for cam in cameras:
                cam['ia'].start()
                doFetch(cam, config)
                cam['ia'].stop()


def maybe_capture_num_frames(cameras, config, num_frames):
    if not num_frames:
        return
    if len(cameras) == 1:
        info(f"Capture {num_frames} frames")
        cam = cameras[0]
        cam['ia'].start()
        while cameras[0]['recordedCount'] < num_frames:
            doFetch(cam, config)
        cam['ia'].stop()
    else:
        # cameras list==0 handled outside/before this function
        while cameras[0]['recordedCount'] < num_frames:
            for cam in cameras:
                cam['ia'].start()
                doFetch(cam, config)
                cam['ia'].stop()


def maybe_capture_auto_bracket(cameras, auto_bracket):
    if not auto_bracket:
        return
    info(
        f"Capture frames for: {auto_bracket.start}..{auto_bracket.stop-1} with steps of {auto_bracket.step} us")
    for exp_time in auto_bracket:
        for cam in cameras:
            apply_param(cam['nm'], 'ExposureTime', exp_time)
            cam['ia'].start()
            with cam['ia'].fetch(timeout=FETCH_TIMEOUT) as buffer:
                cam['frameCount'] += 1
                cam['recordedCount'] += 1
                cam['writer'].store(buffer, cam['nm'])
            cam['ia'].stop()


def data_map(maps, data_format):
    return next(filter(lambda x: x['data_format'] == data_format, maps))


def extract_depth(coord3d_data):
    width = coord3d_data['width']
    height = coord3d_data['height']
    col, row = np.meshgrid(np.arange(width), np.arange(height))
    depth = coord3d_data['data'].reshape(height, width)
    return col, row, depth


def extract_color(bgr8_data):
    width = bgr8_data['width']
    height = bgr8_data['height']
    bgr = bgr8_data['data'].reshape(height, width, 3)
    return bgr[..., ::-1].copy()


def extract_mono(mono_data):
    width = mono_data['width']
    height = mono_data['height']
    mono = mono_data['data'].reshape(height, width)
    return mono


def extract_bayer(bayer_data):
    width = bayer_data['width']
    height = bayer_data['height']
    bayer = bayer_data['data'].reshape(height, width)
    return cvtColor(bayer, BayerPattern)


class Pose:
    """
    Pose configuration
    Contains the camera-to-world matrix and both rotation and position vectors;
    the Pose class ensures that the camera-to-world matrix is always calculated
    from rotation and position vectors in the correct way (Euler XZY), suitable
    to be used with data from Visionary cameras
    """

    def __init__(self):
        """ X/Y/Z translation for camera 2 world transformation"""
        self.position = np.zeros(3, dtype=np.float64)
        """ a/b/c rotation [deg] for camera 2 world transformation"""
        self.orientation = np.zeros(3, dtype=np.float64)
        self.transform_matrix = np.identity(4, dtype=np.float64)

    def get_position(self):
        """returns the current position as offset for X, Y, Z axis"""
        return self.position

    def set_position(self, new_position):
        """set the position as offset for X, Y, Z axis"""
        self.position = np.array(new_position, dtype=np.float64)

    def get_orientation(self):
        """returns the current orientation as rotation around X, Y, Z axis in degrees"""
        return self.orientation

    def set_orientation(self, new_orientation):
        """set position as rotation around X, Y, Z axis in degrees"""
        self.orientation = np.array(new_orientation, dtype=np.float64)

    def get_transform_matrix(self):
        """returns the current transform3d matrix"""
        return self.transform_matrix

    def refresh(self):
        """recalculate the transform3d matrix with the current position and orientation"""
        a, b, c = np.deg2rad(self.orientation)
        m_rot = self._create_from_euler_xyz(a, b, c)
        m_trans = np.hstack((m_rot, self.get_position().reshape(3, 1)))
        m_trans = np.vstack((m_trans, [0, 0, 0, 1]))
        self.transform_matrix = m_trans

    def __init_cs(self, angle):
        """returns sin, cos for a given angle in radians"""
        s = np.sin(angle)
        c = np.cos(angle)
        return s, c

    def _create_rot_3dx(self, angle):
        """creates 3D rotation matrix around x axis"""
        s, c = self.__init_cs(angle)
        m = np.array(
            [[1, 0, 0],
             [0, c, -s],
             [0, s, c]], dtype=np.float64)
        return m

    def _create_rot_3dy(self, angle):
        """creates 3D rotation matrix around y axis"""
        s, c = self.__init_cs(angle)
        m = np.array(
            [[c, 0, s],
             [0, 1, 0],
             [-s, 0, c]], dtype=np.float64)
        return m

    def _create_rot_3dz(self, angle):
        """creates 3D rotation matrix around y axis"""
        s, c = self.__init_cs(angle)
        m = np.array(
            [[c, -s, 0],
             [s, c, 0],
             [0, 0, 1]], dtype=np.float64)
        return m

    def _create_from_euler_xyz(self, x_angle_deg, y_angle_deg, z_angle_deg):
        """creates 3D rotation matrix for angles around the axis X, Y, Z"""
        m_rot_x = self._create_rot_3dx(x_angle_deg)
        m_rot_y = self._create_rot_3dy(y_angle_deg)
        m_rot_z = self._create_rot_3dz(z_angle_deg)
        return m_rot_x @ m_rot_y @ m_rot_z

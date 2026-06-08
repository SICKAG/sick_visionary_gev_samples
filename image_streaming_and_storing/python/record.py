#!/usr/bin/env python3
#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

"""Recorder for multiple (Visionary) GigE cameras"""

from argparse import ArgumentParser
from argparse import RawDescriptionHelpFormatter as rdhf
from logging import INFO, basicConfig, info
from os import environ as env
from sys import exit
from time import time

from common import (ListArgs, config_camera, init_harvester,
                    maybe_capture_auto_bracket, maybe_capture_num_frames,
                    maybe_capture_secs, parse_config, select_devices,
                    setup_camera_objects)
from pickle_harvester import Writer


def main(args):
    harvester = None
    cameras = None
    try:
        config = parse_config(args.config)
        if 'env' in config:
            for key, value in config['env'].items():
                env[key] = str(value)
        harvester = init_harvester()
        device_ids = select_devices(harvester.device_info_list, config)
        cameras = setup_camera_objects(harvester, device_ids)
        if len(cameras) < 1:
            raise RuntimeError(
                "No cameras in the list - please double check whether the config file contains valid serial numbers identifying the cameras to use")

        info("Configure cameras..")
        for cam in cameras:
            try:
                config_camera(cam, config)
            except Exception as err:
                print(
                    "Error while configuring the camera, please double check correctness of the config file ({})".format(err))
                raise

        info("Open storage files..")
        for cam in cameras:
            record_ident = cam['name']
            if args.auto_bracket:
                record_ident += "_AutoBracket"
            cam['writer'] = Writer(record_ident)

        t_start = time()
        maybe_capture_secs(cameras, config, t_start, args.duration)
        maybe_capture_num_frames(cameras, config, args.num_frames)
        maybe_capture_auto_bracket(cameras, args.auto_bracket)
        elapsed = time() - t_start

        for cam in cameras:
            info(f"{cam['name']} received {cam['frameCount']} frames ({cam['frameCount']/elapsed:.1f} Hz), recorded {cam['recordedCount']} frames")
            cam['writer'] = None
            cam['nm'] = None
            cam['ia'].stop()
    finally:
        if cameras:
            for cam in cameras:
                cam['ia'].destroy()
        if harvester:
            harvester.reset()


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__, formatter_class=rdhf)
    parser.add_argument('-c', '--config', required=True,
                        help='Defines which configuration file to be used')
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('-a', '--auto_bracket', required=False, action=ListArgs,
                       help='Run Auto Bracket: <start>,<end>,<step> [us] increasing the exposure time. '
                       'Example: "-a 100,4000,50" from 100us up to 4000us with 50us steps')
    group.add_argument('-d', '--duration', type=float, required=False,
                       help='Duration of the recording [s]. '
                       'Example: "-d 2.5" record for 2.5 seconds')
    group.add_argument('-n', '--num_frames', type=int, required=False,
                       help='Number of frames to being recorded. '
                       'Example: "-n 100" will record exactly 100 frames')
    basicConfig(format="%(levelname)s: %(message)s", level=INFO)
    # Catch any remaining exceptions which might be possibly related to the GenICam feature access (e.g. with an invalid input)
    try:
        exit(main(parser.parse_args()))
    # (sys.exit() itself will raise BaseException and will not interfere with this handler)
    except Exception as err:
        print("ERROR: an exception was raised while executing the script: {}".format(err))
      # (to further debug the exception, re-raising it here might help to get its context if desired: uncomment following line)
      # raise

#!/usr/bin/env python3
#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

"""Print debug output of frames and show image data"""

from argparse import ArgumentParser
from argparse import RawDescriptionHelpFormatter as rdhf
from logging import INFO, basicConfig, info
from os.path import join
from sys import exit

from common import (data_map, extract_bayer, extract_color, extract_depth,
                    extract_mono)
from matplotlib import pyplot as plt
from pickle_harvester import Reader


def display(title, img):
    plt.figure()
    plt.title(title)
    plt.imshow(img)
    plt.waitforbuttonpress()


def main(args):
    with Reader(args.pickle) as reader:
        info(f"Number of frames: {len(reader)}")
        for frame in reader:
            reader.debug_frame(frame)
            for m in frame['maps']:
                dtype = m['data_format']
                if dtype == 'Coord3D_C16':
                    _, _, depth = extract_depth(data_map(frame['maps'], dtype))
                    display('Range/Depth/Z', depth)
                elif dtype == 'BGR8':
                    rgb = extract_color(data_map(frame['maps'], dtype))
                    display('Intensity/Color', rgb)
                elif dtype.startswith('Mono'):
                    mono = extract_mono(data_map(frame['maps'], dtype))
                    display('Intensity', mono)
                elif dtype == 'BayerRG10':
                    rgb = extract_bayer(data_map(frame['maps'], dtype))
                    display('Raw Left/Right (color, demosaiced)', rgb)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__, formatter_class=rdhf)
    parser.add_argument("-p", "--pickle", help="(pickle) input file",
                        default=join('data', '2023_01_25_SICK_Visionary_AP.pickle'))
    basicConfig(format="%(levelname)s: %(message)s", level=INFO)
    exit(main(parser.parse_args()))

#!/usr/bin/env python3
#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

"""Convert Python pickle files to SSR format"""

from argparse import ArgumentParser
from argparse import RawDescriptionHelpFormatter as rdhf
from logging import INFO, basicConfig, error, info
from math import isclose
from os import getcwd, listdir, makedirs, remove
from os.path import basename, getsize, isdir, join
from struct import pack
from sys import exit
from zipfile import ZIP_DEFLATED, ZipFile

import pickle_harvester as ph
import ssr_helper as ssrh
from common import data_map, extract_color, extract_depth, extract_mono
from intrinsics import extract_intrinsics
from PIL import Image

TWO_GB = 1 << 31
FOUR_GB = 1 << 32


def get_decimal_exponent(num):
    for x in range(-9, 9):
        if isclose(10 ** x, num, rel_tol=1e-3):
            return x
    raise RuntimeError('Decimal exponent extraction failed for {}'.format(num))


def create_xml_values(k, width, height, num_frames):
    dec_exp = get_decimal_exponent(k.scale_c)
    max_mm = 65000  # ssr from sopas use 65000
    mapset = ssrh.distanceTemplate.format(
        Z_DECIMAL_EXPONENT=dec_exp,
        # also scale min/max accordingly
        MIN_DISTANCE=1 / k.scale_c, MAX_DISTANCE=max_mm / k.scale_c,
        DTYPE_DISTANCE="uint16") + "\n" + ssrh.intensityTemplate.format(
        MIN_INTENISTY=1, MAX_INTENSITY=4294967295, DTYPE_INTENISTY="uint32")
    return {
        "FX": k.foc_len,
        "FY": k.foc_len * k.aspect_r,
        "CX": k.princ_pt_u,
        "CY": k.princ_pt_v,
        "K1": 0.0,
        "K2": 0.0,
        "P1": 0.0,
        "P2": 0.0,
        "K3": 0.0,
        "PixelSizeX": 1.0,
        "PixelSizeY": 1.0,
        "PixelSizeZ": 1.0,
        "MAPSET": mapset,
        "WIDTH": width,
        "HEIGHT": height,
        "NUM_FRAMES": num_frames
    }


def create_ssr_file(pickle_file, output_name, output_dir=getcwd()):
    reader = ph.Reader(pickle_file)
    num_frames = len(reader)
    info("Number of frames available: %d" % num_frames)
    data_bin_path = join(output_dir, "data.bin")

    with open(data_bin_path, "wb") as file:
        for frame_number, frame in enumerate(reader.get_all_frames(), 1):
            data_formats = [d['data_format'] for d in frame['maps']]
            has_range = 'Coord3D_C16' in data_formats
            has_color = 'BGR8' in data_formats
            has_mono16 = 'Mono16' in data_formats
            if not (has_range and (has_color or has_mono16)):
                raise RuntimeError(
                    "Could not find Intensity + Range in the pickle file")

            coord3d_data = data_map(frame['maps'], 'Coord3D_C16')
            width, height = coord3d_data['width'], coord3d_data['height']
            # We assume intrinsics are equal for all frames
            if frame_number == 1:
                intrinsics = extract_intrinsics(frame)
                t_start_ns = frame['timestamp_ns']
            _, _, depth = extract_depth(coord3d_data)
            img_z = Image.fromarray(depth)
            img_rgb = None
            if has_color:
                rgb = extract_color(data_map(frame['maps'], 'BGR8'))
                img_rgb = Image.fromarray(rgb, mode="RGB")
            elif has_mono16:
                # TODO: this is just workaround, to inject Mono16 into
                #       stereo depthmap ssr type (as rgb image).
                #       We need to add support for "depthmap" (tof)
                #       format to this ssr script.
                mono = extract_mono(data_map(frame['maps'], 'Mono16'))
                mono = np.uint8((mono / np.max(mono)) * 255)
                mono = Image.fromarray(mono, mode='L')
                img_rgb = Image.merge("RGB", (mono, mono, mono))
            if img_z.size != img_rgb.size:
                info('Resize RGB image {} to match the shape of depth image {}'.format(
                    img_rgb.size, img_z.size))
                img_rgb = img_rgb.resize((img_z.size))
            # 'L' 8-bit pixels, black and white
            alpha_channel = Image.new('L', img_rgb.size, 255)
            img_rgb.putalpha(alpha_channel)
            # width * height * (2(Z-Map)+4(RGB Map)) + 16 (header) + 8 (footer)
            frame_size = width * height * 6 + 16 + 8

            file.write(pack("<I", frame_size))  # FrameLength

            ms_since_start = int((frame['timestamp_ns'] - t_start_ns) / 1e6)
            ts_minutes = int(ms_since_start / 1000 / 60)
            remainder_ms = ms_since_start - ts_minutes * 60 * 1000
            ts_seconds = int(remainder_ms / 1000)
            ts_millis = remainder_ms - ts_seconds * 1000

            virtualTime = 0x8D5E6701C009F203  # copied random default form else where
            MinuteMask = 0b0000000000000000000000000000000000000000001111110000000000000000
            SecondsMask = 0b0000000000000000000000000000000000000000000000001111110000000000
            MillisecondsMask = 0b0000000000000000000000000000000000000000000000000000001111111111

            def get_shift(num: int) -> int:
                binary = bin(num)[2:]
                return len(binary) - binary.rindex('1') - 1
            virtualTime &= ~(MinuteMask | SecondsMask |
                             MillisecondsMask)  # set min/sec/ms to 0
            virtualTime |= ts_minutes << get_shift(MinuteMask)
            virtualTime |= ts_seconds << get_shift(SecondsMask)
            virtualTime |= ts_millis << get_shift(MillisecondsMask)
            file.write(pack("<Q", virtualTime))  # Timestamp
            file.write(b"\x02\x00")  # Version
            file.write(pack("<I", frame_number))  # Framenumber
            file.write(b"\x03")  # Dataquality
            file.write(b"\x01")  # device status
            file.write(img_z.tobytes())
            file.write(img_rgb.tobytes())
            file.write(b"\x00\x00\x00\x00")  # CRC
            file.write(pack("<I", frame_size))  # FrameLength2
    reader = None

    xml_values = create_xml_values(intrinsics, width, height, num_frames)
    xml = ssrh.baseXML.format(**xml_values)

    main_xml_path = join(output_dir, "main.xml")
    with open(main_xml_path, "w") as file:
        file.write(xml)

    # Pad file if data.bin or resulting ssr filesize is between 2GB and 4GB
    total_filesize = getsize(main_xml_path) + getsize(data_bin_path)
    if total_filesize >= TWO_GB and total_filesize < FOUR_GB:
        with open(join(output_dir, "data.bin"), "ab") as file:
            file.write(b'\0'*(FOUR_GB - total_filesize))

    # Set compresslevel to 0 to speed up execution or to 1 to get save ~50% of disc space
    with ZipFile(join(output_dir, output_name), 'w', compression=ZIP_DEFLATED, compresslevel=0) as myzip:
        myzip.write(main_xml_path, arcname="main.xml")
        myzip.write(data_bin_path, arcname="data/data.bin")

    # Cleanup
    remove(main_xml_path)
    remove(data_bin_path)


def main(args):
    if isdir(args.pickle_file):
        if args.output:
            error("Cannot use folder for pickle files with -o/--output")
            return 1
        for pfile in listdir(args.pickle_file):
            if pfile.endswith('.pickle'):
                output_dir = join(args.pickle_file)
                makedirs(output_dir, exist_ok=True)
                create_ssr_file(join(args.pickle_file, pfile),
                                pfile + ".ssr", output_dir)
    else:
        output_name = args.output if args.output else basename(
            args.pickle_file) + ".ssr"
        create_ssr_file(args.pickle_file, output_name)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__, formatter_class=rdhf)
    parser.add_argument('pickle_file', help='(pickle) input file or folder')
    parser.add_argument('-o', '--output', help='Output filename for SSR')
    basicConfig(format="%(levelname)s: %(message)s", level=INFO)
    exit(main(parser.parse_args()))

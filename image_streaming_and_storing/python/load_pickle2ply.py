#!/usr/bin/env python3
#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

"""Convert Python pickle files to PLY format"""

import time
from argparse import ArgumentParser
from argparse import RawDescriptionHelpFormatter as rdhf
from logging import INFO, basicConfig, info
from multiprocessing import Pool, cpu_count
from os import listdir
from os.path import isdir, join
from pathlib import Path
from struct import pack
from sys import exit

import numpy as np
from common import Pose, data_map, extract_color, extract_depth, extract_mono
from intrinsics import extract_intrinsics
from pickle_harvester import Reader


def write_ply(filename, points, intensity):
    # TODO Make decision how to store intensity/color data more smart
    #      Currently write data unchanged "as is", however this means
    #      that the value range might appear as all points "almost black"
    #      depending on the recording. Also some tools might have
    #      endianess issues (for example: CloudCompare) when using "intenisty",
    #      in such cases, it is recommended to import it as scalar value.

    # assuming, color image will have always 3 dimensions (for the r/g/b channels)
    is_color = ((intensity.ndim == 3) and (intensity.shape[2] == 3))
    # begin header
    header = [
        'ply\n', 'format binary_little_endian 1.0\n',
        'element vertex %d\n' % np.prod(points.shape[:2]),
        *["property float %s\n" % c for c in "xyz"]]
    # create middle part depending of color/intensity
    if is_color:
        vtypes = np.dtype([('x', '<f4'), ('y', '<f4'), ('z', '<f4'),
                          ('red', 'u1'), ('green', 'u1'), ('blue', 'u1')])
        header += [*["property %s\n" %
                     c for c in ("uchar red", "uchar green", "uchar blue")]]
    else:
        vtypes = np.dtype([('x', '<f4'), ('y', '<f4'),
                          ('z', '<f4'), ('intensity', '<u2')])
        header += ["property ushort intensity\n"]
    # finish header
    header += ['element resolution 1\n',
               *["property ushort %s\n" % c for c in ("width", "height")],
               'end_header\n']
    # create vertices data
    vertices = np.empty(np.prod(points.shape[:2]), dtype=vtypes)
    vertices['x'], vertices['y'], vertices['z'] = points.reshape(-1, 3).T
    if is_color:
        vertices['red'], vertices['green'], vertices['blue'] = intensity.reshape(
            -1, 3).T
    else:
        vertices['intensity'] = intensity.flatten()
    # write ply data to file
    with open(filename, 'wb') as f:
        info("Writing %s" % filename)
        f.write("".join(header).encode('utf-8'))
        vertices.tofile(f)
        width, height = points.shape[:2]
        f.write(pack('<HH', width, height))


def generate_pointcloud(k, col, row, depth, trans_matrix=None):
    xp = (col - k.princ_pt_u) / k.foc_len
    yp = (row - k.princ_pt_v) / (k.foc_len * k.aspect_r)

    scaled_c = depth * k.scale_c + k.offset_c

    xc = xp * scaled_c
    yc = yp * scaled_c
    zc = scaled_c
    points = np.stack([xc, yc, zc], axis=-1)

    if trans_matrix is None:
        return points

    points_flat = points.reshape(-1, 3)
    points_hom = np.hstack((points_flat, np.ones((points_flat.shape[0], 1))))
    return np.dot(trans_matrix, points_hom.T).T[:, :3].reshape(points.shape)


def process_frame(frame, trans_matrix, outfile):
    data_formats = [d['data_format'] for d in frame['maps']]
    has_range = 'Coord3D_C16' in data_formats
    has_color = 'BGR8' in data_formats
    has_mono16 = 'Mono16' in data_formats
    if not (has_range and (has_color or has_mono16)):
        raise RuntimeError(
            "Could not find Intensity + Range in the pickle file")

    col, row, depth = extract_depth(data_map(frame['maps'], 'Coord3D_C16'))
    intensity = None
    if has_color:
        intensity = extract_color(data_map(frame['maps'], 'BGR8'))
    elif has_mono16:
        intensity = extract_mono(data_map(frame['maps'], 'Mono16'))
    intrinsics = extract_intrinsics(frame)
    pointcloud = generate_pointcloud(intrinsics, col, row, depth, trans_matrix)

    # Digression: on current Visionary camera models the dimensions of the range and intensity components are either exactly equal,
    # or the intensity component is twice as big. This needs to be considered when overlaying the intensity "colors"
    # on the point cloud (range values).
    # For now let's just rely on this - the approach might get even a bit more complicated if in future some of the
    # cameras support other size-affecting features such as cropping. On the other hand, on cameras where the range
    # and intensity image are always equal sized (such as on Visionary-B Two), the scaling topic can be ignored.
    intensity_scale_x = int(intensity.shape[0] / depth.shape[0])
    intensity_scale_y = int(intensity.shape[1] / depth.shape[1])
    # For simplicity we'll pick directly a single intensity pixel for overlay even if the intensity is scaled,
    # more advanced approach could involve some kind of interpolation.
    intensity_scaled = intensity[::intensity_scale_y, ::intensity_scale_x]

    write_ply(outfile, pointcloud, intensity_scaled)


def export(pickle_file, skip, convert, pose, outfile_ply):
    trans_matrix = pose.get_transform_matrix()
    with Reader(pickle_file) as reader:
        info(f"Number of frames available: {len(reader)}")
        frames = reader.get_frames(skip, convert)
        with Pool(cpu_count()) as pool:
            pool.starmap(process_frame, [
                         (frame, trans_matrix, outfile_ply % idx) for idx, frame in enumerate(frames)])
            # time for buffered prints, after all threads has joined
            time.sleep(0.2)


def main(args):
    pose = Pose()
    pose.set_orientation([args.rotation_x, args.rotation_y, args.rotation_z])
    pose.set_position(
        [args.translation_x, args.translation_y, args.translation_z])
    pose.refresh()
    if isdir(args.pickle_file):
        for pickle_file in listdir(args.pickle_file):
            if pickle_file.lower().endswith(".pickle"):
                outfile_ply = join(args.pickle_file, Path(
                    pickle_file).stem + "_%d.ply")
                export(join(args.pickle_file, pickle_file),
                       args.skip, args.convert, pose, outfile_ply)
    else:
        outfile_ply = Path(args.pickle_file).stem + "_%d.ply"
        export(args.pickle_file, args.skip, args.convert, pose, outfile_ply)


if __name__ == "__main__":
    parser = ArgumentParser(description=__doc__, formatter_class=rdhf)
    parser.add_argument("pickle_file", help="(pickle) input file or folder")
    parser.add_argument(
        "-s", "--skip", help="Number of frames to be skipped at beginning of file", default=0, type=int)
    parser.add_argument(
        "-c", "--convert", help="Number of frames to be converted", default=0, type=int)

    parser.add_argument("-rx", "--rotation_x",
                        help="Rotation of the camera around the X-axis (in degrees)", type=float, default=0.0)
    parser.add_argument("-ry", "--rotation_y",
                        help="Rotation of the camera around the Y-axis (in degrees)", type=float, default=0.0)
    parser.add_argument("-rz", "--rotation_z",
                        help="Rotation of the camera around the Z-axis (in degrees)", type=float, default=0.0)
    parser.add_argument("-tx", "--translation_x",
                        help="Translation of the camera along the X-axis (in mm)", type=float, default=0.0)
    parser.add_argument("-ty", "--translation_y",
                        help="Translation of the camera along the Y-axis (in mm)", type=float, default=0.0)
    parser.add_argument("-tz", "--translation_z",
                        help="Translation of the camera along the Z-axis (in mm)", type=float, default=0.0)

    basicConfig(format="%(levelname)s: %(message)s", level=INFO)
    exit(main(parser.parse_args()))

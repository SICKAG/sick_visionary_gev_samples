#
# Copyright (c) 2025 SICK AG, Waldkirch
#
# SPDX-License-Identifier: MIT

"""Read/Write harvester buffers into/from pickle files"""

import struct as _struct
from logging import info, debug
from pickle import dump, load
import pickle as _pickle
from time import strftime, localtime


# ---------------------------------------------------------------------------
# Fast pickle-object skipper
# ---------------------------------------------------------------------------
# Pickle opcode constants used by the scanner below.
_OP_PROTO        = _pickle.PROTO[0]
_OP_FRAME        = _pickle.FRAME[0]
_OP_STOP         = _pickle.STOP[0]
_OP_SHORT_BINSTR = _pickle.SHORT_BINSTRING[0]
_OP_BINUNICODE   = _pickle.BINUNICODE[0]
_OP_SHORT_BINUNI = _pickle.SHORT_BINUNICODE[0]
_OP_BINUNICODE8  = _pickle.BINUNICODE8[0]
_OP_SHORT_BINBYT = _pickle.SHORT_BINBYTES[0]
_OP_BINBYTES     = _pickle.BINBYTES[0]
_OP_BINBYTES8    = _pickle.BINBYTES8[0]
_OP_BININT1      = _pickle.BININT1[0]
_OP_BININT2      = _pickle.BININT2[0]
_OP_BININT       = _pickle.BININT[0]
_OP_LONG1        = _pickle.LONG1[0]
_OP_LONG4        = _pickle.LONG4[0]
_OP_BINFLOAT     = _pickle.BINFLOAT[0]
_OP_BINPUT       = _pickle.BINPUT[0]
_OP_LONG_BINPUT  = _pickle.LONG_BINPUT[0]
_OP_BINGET       = _pickle.BINGET[0]
_OP_LONG_BINGET  = _pickle.LONG_BINGET[0]
_OP_BYTEARRAY8   = _pickle.BYTEARRAY8[0]


class Writer:
    """Class for storing harvesters buffer into pickle file"""

    def __init__(self, record_name="UNNAMED"):
        self.nodes_wl = None
        self.buffer_wl = None
        self.maps_wl = None
        self.record_name = record_name
        self.wl_written = False
        self.file = None

        # TODO(dedekst): How to handle chunk data dynamically?
        #     buffer.module.is_containing_chunk_data()
        #     buffer.module.num_chunks
        #     for n in nodeMap.nodes:
        #         print(n.node.name) # regex for "Chunk...Selector"

    def store(self, buffer, nodeMap):
        """Stores the provided buffer/frame to file"""
        if not self.wl_written:
            self._create_wl()
            self._store_wl()

        for name, source in self.nodes_wl.items():
            try:
                self._dump(buffer, nodeMap, source)
            except:
                # e.g. ExposureTime cannot be read while ExposureAuto is running
                dump("N/A", self.file)

        for name, source in self.buffer_wl.items():
            try:
                self._dump(buffer, nodeMap, source)
            except Exception as err:
                raise RuntimeError(f"Failed to buffer info {name}") from err

        for component in buffer.payload.components:
            for name, source in self.maps_wl.items():
                try:
                    self._dump(buffer, nodeMap, source, component)
                except Exception as err:
                    raise RuntimeError(f"Failed to component (map) info {name}") from err

    def _create_wl(self):
        """White lists define *what* is stored"""
        # yapf: disable
        self.nodes_wl = {
            # these features are optional and will be replaced with "N/A" if an exception occurs
            "AcquisitionFrameRate": 'nodeMap.AcquisitionFrameRate.value',
            "ExposureTime": 'nodeMap.ExposureTime.value',
            "ExposureAuto": 'nodeMap.ExposureAuto.value',
            "ExposureAutoFrameRateMin": 'nodeMap.ExposureAutoFrameRateMin.value',
            "FieldOfView": 'nodeMap.FieldOfView.value',
            "MultiSlopeMode": 'nodeMap.MultiSlopeMode.value',
            "DataFilterEnable": 'nodeMap.Scan3dDataFilterEnable.value',
            "DepthValidationFilterLevel": 'nodeMap.Scan3dDataFilterSelector.value=\'ValidationFilter\';nodeMap.Scan3dDepthValidationFilterLevel.value',
        }
        self.buffer_wl = {
            # these features are mandatory! An exception will occurs if they cannot be read
            'frame_id': 'buffer.module.frame_id',
            'timestamp_ns': 'buffer.timestamp_ns',
            'numComponents': 'len(buffer.payload.components)',
            'FocalLength': 'nodeMap.ChunkScan3dFocalLength.value',
            'AspectRatio': 'nodeMap.ChunkScan3dAspectRatio.value',
            'PrincipalPointU': 'nodeMap.ChunkScan3dPrincipalPointU.value',
            'PrincipalPointV': 'nodeMap.ChunkScan3dPrincipalPointV.value',
            # select CoordinateA
            'CoordinateScaleA': 'nodeMap.ChunkScan3dCoordinateSelector.value=\'CoordinateA\';nodeMap.ChunkScan3dCoordinateScale.value',
            'CoordinateOffsetA': 'nodeMap.ChunkScan3dCoordinateOffset.value',
            # select CoordinateB
            'CoordinateScaleB': 'nodeMap.ChunkScan3dCoordinateSelector.value=\'CoordinateB\';nodeMap.ChunkScan3dCoordinateScale.value',
            'CoordinateOffsetB': 'nodeMap.ChunkScan3dCoordinateOffset.value',
            # select CoordinateC
            'CoordinateScaleC': 'nodeMap.ChunkScan3dCoordinateSelector.value=\'CoordinateC\';nodeMap.ChunkScan3dCoordinateScale.value',
            'CoordinateOffsetC': 'nodeMap.ChunkScan3dCoordinateOffset.value',
            # anchor to reference
            'RotationX': 'nodeMap.ChunkScan3dCoordinateReferenceSelector.value=\'RotationX\';nodeMap.ChunkScan3dCoordinateReferenceValue.value',
            'RotationY': 'nodeMap.ChunkScan3dCoordinateReferenceSelector.value=\'RotationY\';nodeMap.ChunkScan3dCoordinateReferenceValue.value',
            'RotationZ': 'nodeMap.ChunkScan3dCoordinateReferenceSelector.value=\'RotationZ\';nodeMap.ChunkScan3dCoordinateReferenceValue.value',
            'TranslationX': 'nodeMap.ChunkScan3dCoordinateReferenceSelector.value=\'TranslationX\';nodeMap.ChunkScan3dCoordinateReferenceValue.value',
            'TranslationY': 'nodeMap.ChunkScan3dCoordinateReferenceSelector.value=\'TranslationY\';nodeMap.ChunkScan3dCoordinateReferenceValue.value',
            'TranslationZ': 'nodeMap.ChunkScan3dCoordinateReferenceSelector.value=\'TranslationZ\';nodeMap.ChunkScan3dCoordinateReferenceValue.value',
        }
        self.maps_wl = {
            # these values are read from each component
            'data_format': 'c.data_format',
            'width': 'c.width',
            'height': 'c.height',
            'delivered_image_height': 'c.delivered_image_height',
            'data': 'c.data',  # data is numpy array
        }
        # yapf: enable

    def _store_wl(self):
        if self.wl_written:
            raise RuntimeError(
                "The white lists contract must be written once at beginning of the pickle recording"
            )
        filename = strftime("%Y-%m-%d_%H-%M-%S_", localtime()) + self.record_name + ".pickle"
        info(f"Open file for writing: {filename}")

        self.file = open(filename, 'wb')
        # store the WhiteList contracts
        dump(self.nodes_wl, self.file)
        dump(self.buffer_wl, self.file)
        dump(self.maps_wl, self.file)
        self.wl_written = True

    def _dump(self, buffer, nodeMap, sources, c=None):
        src_list = sources.split(';')
        for source in src_list[:-1]:
            exec(source)
        exec(f"dump({src_list[-1]}, self.file)")

    def __del__(self):
        if self.file:
            self.file.close()


class Reader:
    """Class for reading harvesters buffer from pickle file"""

    def __init__(self, filename):
        info(f"Open file for reading: {filename}")
        self.file = open(filename, 'rb')
        self.num_frames = None
        # restore the WhiteList contracts
        self.nodes_wl, self.buffer_wl, self.maps_wl = self._load_wl()

    def __enter__(self):
        return self

    def __exit__(self, type, value, traceback):
        self.file.close()

    def __len__(self):
        return self._get_number_of_frames()

    def __iter__(self):
        self._rewind_to_start_of_frames()
        return self

    def __next__(self):
        frame = self._restore()
        if frame:
            return frame
        else:
            self._rewind_to_start_of_frames()
            raise StopIteration

    def __del__(self):
        self.file.close()

    def get_next_frame(self, skip_read=False):
        """Restores and returns the next frame from file. Alias for restore()"""
        return self._restore(skip_read=skip_read)

    def get_all_frames(self):
        """Load all available frames from file.
        Returns a list of all frames which could be read.
        Will rewind to beginning of the file in the end.
        """
        frames = list()
        while self.file.read(1):
            self.file.seek(-1, 1)
            frames.append(self.get_next_frame())
        self._rewind_to_start_of_frames()
        return frames

    def get_frames(self, skip, n=0):
        """Load n frames after skip frames were skipped"""
        if n == 0:
            n = self._get_number_of_frames()

        self._rewind_to_start_of_frames()
        self._skip_frames(skip)
        frames = list()
        while self.file.read(1) and len(frames) < n:
            self.file.seek(-1, 1)
            frames.append(self.get_next_frame())
        return frames

    def _get_number_of_frames(self):
        # this is read-only, number of contained frames will never change
        if self.num_frames is None:
            self.num_frames = sum(1 for _ in iter(lambda: self.get_next_frame(skip_read=True), None))
            self._rewind_to_start_of_frames()
        return self.num_frames

    def _restore(self, skip_read=False):
        """Restores and returns the next frame from file"""

        # Check EOF condition
        if not self.file.read(1):
            return None
        self.file.seek(-1, 1)

        frame = {}
        for node_name in self.nodes_wl:
            if skip_read:
                self._skip_pickle_object()
            else:
                frame.update({node_name: load(self.file)})

        for buf_info in self.buffer_wl:
            if skip_read:
                if buf_info == 'numComponents':
                    # Must load numComponents even when skipping so we know
                    # how many per-component map entries to skip below.
                    frame.update({buf_info: load(self.file)})
                else:
                    self._skip_pickle_object()
            else:
                frame.update({buf_info: load(self.file)})

        maps = list()
        for i in range(frame['numComponents']):
            tmp = {}
            for mapInfo in self.maps_wl:
                if skip_read:
                    self._skip_pickle_object()
                else:
                    tmp.update({mapInfo: load(self.file)})
            maps.append(tmp)
        frame.update({'maps': maps})
        return frame

    def debug_frame(self, frame):
        """Print all helpful information contained in the given frame"""
        info("##################################")
        info("######## Frame Debug Info ########")
        info("##################################")
        for k, v in frame.items():
            if k != 'maps':
                info("# {}: {}".format(k, v))
        info("# Contained maps:")
        for m in frame['maps']:
            info("#   Format {}, {}x{}({}), {}bpp, total items:{}".format(
                m['data_format'], m['width'], m['height'], m['delivered_image_height'],
                m['data'].itemsize, m['data'].size))
        info("##################################")

    def _skip_pickle_object(self):
        """Scan and skip one pickled object starting at the current file position.

        Minimises self.file.read() calls: the only reads are the single opcode byte and
        the length-header bytes of variable-length opcodes (we need those to know
        how far to seek).  Everything else — including fixed-width argument fields,
        string/bytes payloads, and array data — is skipped with self.file.seek() so no
        data is transferred from network storage.

        Returns True on success, False if EOF is encountered before STOP.
        """
        f = self.file
        while True:
            op_byte = f.read(1)
            if not op_byte:
                return False
            op = op_byte[0]

            if op == _OP_STOP:
                return True
            elif op == _OP_PROTO:
                f.seek(1, 1)         # version byte — value unused, seek instead of read
            elif op == _OP_FRAME:
                f.seek(8, 1)         # 8-byte frame-size field — value unused; opcodes
                #                    # follow immediately inside the frame, keep scanning
            elif op in (_OP_SHORT_BINSTR, _OP_SHORT_BINUNI, _OP_SHORT_BINBYT):
                n = f.read(1)[0]     # length byte must be read to know skip distance
                f.seek(n, 1)
            elif op == _OP_BINUNICODE:
                n = _struct.unpack('<I', f.read(4))[0]
                f.seek(n, 1)
            elif op == _OP_BINUNICODE8:
                n = _struct.unpack('<Q', f.read(8))[0]
                f.seek(n, 1)
            elif op == _OP_BINBYTES:
                n = _struct.unpack('<I', f.read(4))[0]
                f.seek(n, 1)
            elif op == _OP_BINBYTES8 or op == _OP_BYTEARRAY8:
                n = _struct.unpack('<Q', f.read(8))[0]
                f.seek(n, 1)
            elif op == _OP_BININT1:   # 'K'  1-byte arg
                f.seek(1, 1)
            elif op == _OP_BININT2:        # 'M'  2-byte arg
                f.seek(2, 1)
            elif op == _OP_BININT:         # 'J'  4-byte arg
                f.seek(4, 1)
            elif op == _OP_BINFLOAT:       # 'G'  8-byte arg
                f.seek(8, 1)
            elif op == _OP_LONG1:
                n = f.read(1)[0];  f.seek(n, 1)   # read 1-byte length, seek data
            elif op == _OP_LONG4:
                n = _struct.unpack('<I', f.read(4))[0];  f.seek(n, 1)
            elif op in (_OP_BINPUT, _OP_BINGET):
                f.seek(1, 1)
            elif op in (_OP_LONG_BINPUT, _OP_LONG_BINGET):
                f.seek(4, 1)
            # All remaining opcodes (NONE, TRUE, FALSE, MARK, TUPLE*, LIST, DICT,
            # REDUCE, BUILD, GLOBAL/STACK_GLOBAL, NEWOBJ, MEMOIZE, …) are single
            # bytes with no trailing data; just continue the loop.

    def _skip_frames(self, num):
        for i in range(num):
            _ = self.get_next_frame(skip_read=True)

    def _rewind_to_start_of_frames(self):
        self.file.seek(0)
        _ = self._load_wl()

    def _load_wl(self):
        # load the WhiteList contracts, from beginning of file
        if self.file.tell() != 0:
            raise RuntimeError(
                "Loading WL are only expected to be read from beginning of file, but file pos is: {}"
                .format(self.file.tell(0)))
        nodes_wl = load(self.file)
        buffer_wl = load(self.file)
        maps_wl = load(self.file)
        return nodes_wl, buffer_wl, maps_wl

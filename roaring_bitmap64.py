import math
import struct
import sys
from typing import BinaryIO

from pyroaring import BitMap64


class ShapeSet:
    def __init__(self):
        self.halves = None
        self.shapes = None

    def load_binary(self, filename):
        with open(filename, "rb") as file:
            self.halves = self._read_shapes(file)
            self.shapes = self._read_shapes(file)

    def _read_shapes(self, file: BinaryIO, batch_size=1024):
        shapes = BitMap64()
        size_bytes = file.read(4)
        (size,) = struct.unpack("I", size_bytes)
        for i in range(math.ceil(size / batch_size)):
            cur_size = min(batch_size, size - i * batch_size)
            batch_bytes = file.read(cur_size * 8)
            batch_shapes = struct.unpack(f"{cur_size}Q", batch_bytes)
            shapes.update(batch_shapes)
        return shapes

    def load_bitmap(self, filename):
        with open(filename, "rb") as file:
            halves_size = int.from_bytes(file.read(4), byteorder="little")
            self.halves = BitMap64.deserialize(file.read(halves_size))
            shapes_size = int.from_bytes(file.read(4), byteorder="little")
            self.shapes = BitMap64.deserialize(file.read(shapes_size))
            print("halves_size:", halves_size)
            print("shapes_size:", shapes_size)

    def save_bitmap(self, filename):
        with open("5_halves_stable.rbm", "wb") as file:
            self.halves.run_optimize()
            halves_bytes = self.halves.serialize()
            halves_size = len(halves_bytes)
            file.write(struct.pack("I", halves_size))
            file.write(halves_bytes)
            print("halves_size:", halves_size)

        with open("5_shapes_unstable.rbm", "wb") as file:
            self.shapes.run_optimize()
            shapes_bytes = self.shapes.serialize()
            shapes_size = len(shapes_bytes)
            file.write(struct.pack("I", shapes_size))
            file.write(shapes_bytes)
            print("shapes_size:", shapes_size)


if __name__ == "__main__":
    # 使用示例
    shape_set = ShapeSet()
    shape_set.load_binary(sys.argv[1])
    print("Halves:", len(shape_set.halves))
    print("Shapes:", len(shape_set.shapes))
    shape_set.save_bitmap(sys.argv[2])
    del shape_set

    shape_set = ShapeSet()
    shape_set.load_bitmap(sys.argv[2])
    print("Halves:", len(shape_set.halves))
    print("Shapes:", len(shape_set.shapes))
    shape_set.save_bitmap(sys.argv[3])

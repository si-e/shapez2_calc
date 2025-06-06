import math
import struct
from typing import BinaryIO

from pyroaring import BitMap64


class ShapeSet:
    @classmethod
    def from_binary(cls, filename):
        def _read_shapes(file: BinaryIO, batch_size=65536):
            shapes = BitMap64()
            size_bytes = file.read(4)
            (size,) = struct.unpack("I", size_bytes)
            for i in range(math.ceil(size / batch_size)):
                cur_size = min(batch_size, size - i * batch_size)
                batch_bytes = file.read(cur_size * 8)
                batch_shapes = struct.unpack(f"{cur_size}Q", batch_bytes)
                shapes.update(batch_shapes)
            return shapes

        shape_set = cls()
        with open(filename, "rb") as file:
            shape_set.halves = _read_shapes(file)
            shape_set.shapes = _read_shapes(file)
        return shape_set

    @classmethod
    def from_bitmap(cls, filename):
        shape_set = cls()
        with open("5_halves_stable.rbm", "rb") as file:
            shape_set.halves = BitMap64.deserialize(file.read())
        with open("5_shapes_unstable.rbm", "rb") as file:
            shape_set.shapes = BitMap64.deserialize(file.read())
        return shape_set

    def __init__(self):
        self.halves = None
        self.shapes = None

    def save_bitmap(self, filename):
        with open("5_halves_stable.rbm", "wb") as file:
            self.halves.run_optimize()
            halves_bytes = self.halves.serialize()
            halves_size = len(halves_bytes)
            # file.write(struct.pack("I", halves_size))
            file.write(halves_bytes)
            print("halves_rbm_file_size:", halves_size, "Bytes")

        with open("5_shapes_unstable.rbm", "wb") as file:
            self.shapes.run_optimize()
            shapes_bytes = self.shapes.serialize()
            shapes_size = len(shapes_bytes)
            # file.write(struct.pack("I", shapes_size))
            file.write(shapes_bytes)
            print("shapes_rbm_file_size:", shapes_size, "Bytes")


if __name__ == "__main__":
    import sys

    binary_file = sys.argv[1] if len(sys.argv) > 1 else "dump5.bin"
    bitmap_file = sys.argv[2] if len(sys.argv) > 2 else "dump5.rbm"

    shape_set = ShapeSet.from_binary(binary_file)
    print("Halves number:", len(shape_set.halves))
    print("Shapes number:", len(shape_set.shapes))
    shape_set.save_bitmap(bitmap_file)
    del shape_set

    # 使用示例
    shape_set = ShapeSet.from_bitmap(bitmap_file)
    print("Halves number:", len(shape_set.halves))
    print("Shapes number:", len(shape_set.shapes))

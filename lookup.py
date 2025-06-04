import struct
import sys


class ShapeSet:
    def __init__(self):
        self.halves = []
        self.shapes = []

    def load(self, filename):
        with open(filename, "rb") as file:
            # 读取第一个 vector 的大小
            size_bytes = file.read(4)
            if size_bytes:
                (size,) = struct.unpack("I", size_bytes)
                self.halves = self._read_shapes(file, size)

            # 读取第二个 vector 的大小
            size_bytes = file.read(4)
            if size_bytes:
                (size,) = struct.unpack("I", size_bytes)
                self.shapes = self._read_shapes(file, size)

    def _read_shapes(self, file, size):
        shapes = []
        for _ in range(size):
            # 假设 Shape 是 uint64 类型，即 8 字节
            shape_bytes = file.read(8)
            if shape_bytes:
                (shape,) = struct.unpack("Q", shape_bytes)
                shapes.append(shape)
        return shapes


# 使用示例
filename = sys.argv[1]
shape_set = ShapeSet()
shape_set.load(filename)
print("Halves:", len(shape_set.halves))
print("Shapes:", len(shape_set.shapes))

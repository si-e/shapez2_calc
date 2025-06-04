#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef CONFIG_LAYER
#define CONFIG_LAYER 4
#endif

#ifndef CONFIG_PART
#define CONFIG_PART 4
#endif


namespace Shapez {

using std::size_t;

// The type of the shape at each cell.
// Color doesn't matter, because
//   1) For normal shapes, we can always paint them at the very beginning.
//   2) For crystal, the shape produced by crystal generator has no gaps
//      or pins. So it is always possible to get the desired color for
//      each quarter, by generating crystal layer by layer; and then swap
//      the quarters into one shape.
// There is no need to distinguish Circle/Square/etc, because we can track
// back to where the shapes are staked, and choose the correct type there.
enum class Type {
    Empty,
    Pin,
    Shape,
    Crystal,
};

inline char toChar(Type type) {
    switch (type) {
        case Type::Empty:   return '-';
        case Type::Pin:     return 'P';
        case Type::Shape:   return 'S';
        case Type::Crystal: return 'c';
    }
    std::unreachable();
}

inline Type parseType(char c) {
    switch (c) {
        case '-': return Type::Empty;
        case 'P': return Type::Pin;
        case 'c': return Type::Crystal;
        default:  return Type::Shape;
    }
}

// Utility function that repeats a bit pattern `count` times.
// The bit pattern has value `val` and is of bit-width `width`
template <typename T>
constexpr T repeat(T val, size_t width, size_t count) {
    T ret = 0;
    for (size_t i = 0; i < count; ++i) {
        ret <<= width;
        ret |= val;
    }
    return ret;
}

// A shape.
// This is a compact array. Each element occupies 2 bits (the size of Type).
// The first index is layer; the second index is the part in the layer.
// All the bitmasks referred in this class is a compact array with the same
// layout, and each element takes value from 0b00 and 0b11.
struct Shape {
    // layers
    constexpr static size_t LAYER = CONFIG_LAYER;
    // parts in each layer
    constexpr static size_t PART = CONFIG_PART;

    static_assert(LAYER * LAYER * 2 <= 64);
    using T = std::conditional_t<LAYER * PART * 2 <= 32, uint32_t, uint64_t>;

    T value = 0;

    constexpr Type get(size_t layer, size_t part) const {
        size_t idx = layer * PART + part;
        return Type((value >> (idx * 2)) & T(3));
    }

    constexpr void set(size_t layer, size_t part, Type type) {
        size_t idx = layer * PART + part;
        value &= ~(T(3) << (idx * 2));
        value |= T(type) << (idx * 2);
    }

    std::string toString(bool withColor = false) const {
        size_t len = LAYER * PART * (withColor + 1) + LAYER - 1;
        std::string repr(len, 0);
        size_t p = 0;
        for (size_t layer = 0; layer < LAYER; ++layer) {
            if (layer) {
                repr[p++] = ':';
            }
            for (size_t part = 0; part < PART; ++part) {
                Type type = get(layer, part);
                repr[p++] = toChar(type);
                if (withColor) {
                    if (type == Type::Empty || type == Type::Pin) {
                        repr[p++] = '-';
                    } else {
                        repr[p++] = 'w';
                    }
                }
            }
        }
        return repr;
    }

    constexpr bool operator==(const Shape&) const = default;
    constexpr bool operator<(const Shape& o) const {
        return value < o.value;
    }

    constexpr Shape() = default;
    constexpr explicit Shape(T v) : value(v) {}

    Shape(std::string_view repr) {
        constexpr size_t shortLen = LAYER * PART + LAYER - 1;
        constexpr size_t fullLen = 2 * LAYER * PART + LAYER - 1;
        bool isFull = repr.size() == fullLen;
        if (repr.size() != shortLen && repr.size() != fullLen) {
            throw std::runtime_error("incorrect len");
        }
        size_t p = 0;
        for (size_t layer = 0; layer < LAYER; ++layer) {
            if (layer) {
                if (repr[p++] != ':') {
                    throw std::runtime_error("missing :");
                }
            }
            for (size_t part = 0; part < PART; ++part) {
                set(layer, part, parseType(repr[p++]));
                p += isFull;
            }
        }
    }

    // Return a bitmask of all the cells that have the given type.
    template <Type type>
    constexpr T find() const {
        T repeated = repeat<T>(T(type), 2, LAYER * PART);
        T inequal = value ^ repeated;
        T inequal0 = inequal & repeat<T>(1, 2, LAYER * PART);
        T inequal1 = inequal & repeat<T>(2, 2, LAYER * PART);
        inequal = inequal | (inequal0 << 1) | (inequal1 >> 1);
        T equal = ~inequal & repeat<T>(3, 2, LAYER * PART);
        return equal;
    }

    constexpr Shape operator&(T mask) const {
        return Shape(value & mask);
    }

    // number of non-empty layers
    constexpr size_t layers() const {
        size_t l = 0;
        for (T v = value; l < LAYER && v; ++l, v >>= 2 * PART);
        return l;
    }

    // rotate the shape N times
    constexpr Shape rotate(size_t N = 1) const {
        T mask = repeat<T>(repeat<T>(3, 2, N), PART * 2, LAYER);
        return Shape(((value & mask) << (2 * (PART - N)))
                   | ((value & ~mask) >> (2 * N)));
    }

    // swap two shapes together (assume there is no conflict)
    constexpr Shape operator|(Shape other) const {
        return Shape(value | other.value);
    }

    // process the shape by crystal generator
    constexpr Shape crystalize() const {
        T mask = find<Type::Empty>() | find<Type::Pin>();
        mask &= repeat<T>(3, 2, layers() * PART);
        return Shape((mask & repeat<T>(T(Type::Crystal), 2, PART * LAYER))
                   | ((value & ~mask)));
    }

    // Returns a bitmask of all the parts that are supported
    // This is implemented with DFS from the ground.
    // This implementation is different from the game. In the game, if
    // A supports B, and B supports A, then A and B are both considered
    // supported regardless of their relation to other parts of the shape.
    // In this implemention, A or B must be supported by other parts to be
    // considered supported.
    // For example, CuCu----:--------:crCu----:crP-----:crCu----} is
    // creatable in the game. This is considered a bug (SPZ2-3399). Therefore,
    // we go straight to the correct behavior and don't allow such shapes.
    constexpr T supportedPart() const;

    // Stack another connected shape on top of this shape.
    // Assume all the crystals have already broken.
    // Because there is no crystal in the upper shape, connected shape must
    // reside in one layer.
    // A general shape can be decomposed into multiple connected shapes.
    // Stacking the whole shape on another shape is equivalent to sequentially
    // stacking each connected pieces from bottom to top.
    constexpr Shape stack(Shape oneLayer) const {
        T empty = find<Type::Empty>();
        T v = oneLayer.value;
        // If there is no room at the very top, the shape will exceed layer
        // limit after stacking, and the newly stacked part is discarded.
        if (v & ~empty) {
            return *this;
        }
        // Fall if both are true
        // 1) no part on the first layer
        // 2) no part is supported by an existing part
        while (!(v & repeat<T>(3, 2, PART)) && !((v >> (2 * PART)) & ~empty)) {
            v >>= 2 * PART;
        }
        return Shape(value | v);
    }

    // Apply shape gravity rules to a shape
    constexpr Shape collapse() const {
        T mask_x1 = value & repeat<T>(1, 2, LAYER * PART);
        mask_x1 = mask_x1 | (mask_x1 << 1);
        T mask_1x = (value >> 1) & repeat<T>(1, 2, LAYER * PART);
        mask_1x = mask_1x | (mask_1x << 1);
        T mask_c = mask_1x & mask_x1;  // 0b11 for Type::Crystal
        T mask_s = mask_1x & ~mask_x1;  // 0b10 for Type::Shape
        T mask_sc = mask_1x;
        T mask_psc = mask_1x | mask_x1;
        T supported = mask_psc & repeat<T>(3, 2, PART);
        while (true) {
            T last = supported;
            T supported_up = supported << (2 * PART);
            Shape sc{supported & mask_sc};
            T supported_left_right = sc.rotate(1).value | sc.rotate(PART - 1).value;
            T supported_down = (supported & mask_c) >> (2 * PART);
            supported |= (mask_psc & supported_up) | (mask_sc & supported_left_right) | (mask_c & supported_down);
            if (supported == last) {
                break;
            }
        }
        // No change to supported parts
        Shape ret{value & supported};
        // Falling parts
        T v = value & ~supported;
        // Crystals in the falling parts break
        v &= ~find<Type::Crystal>();

        // Stack the falling parts on top of the supported parts,
        // from bottom to top
        for (size_t layer = 0; layer < LAYER; ++layer) {
            for (size_t part = 0; part < PART; ++part) {
                Type type = Shape(v).get(layer, part);
                if (type == Type::Pin) {
                    T connected = T(3) << ((layer * PART + part) * 2);
                    T t = v & connected;
                    v &= ~connected;
                    ret = ret.stack(Shape(t));
                } else if (type == Type::Shape) {
                    T connected = T(3) << ((layer * PART + part) * 2);
                    // Find connected parts
                    while (true) {
                        T last = connected;
                        Shape s{connected};
                        T connected_left_right = s.rotate(1).value | s.rotate(PART - 1).value;
                        connected |= mask_s & connected_left_right;
                        if (connected == last) {
                            break;
                        }
                    }
                    T t = v & connected;
                    v &= ~connected;
                    ret = ret.stack(Shape(t));
                }
            }
        }
        return ret;
    }

    // break crystals covered by the Mask, as well as all the crystals
    // connected to them
    template <T Mask>
    constexpr Shape breakCrystals() const {
        T mask_c = (value & (value >> 1)) & repeat<T>(1, 2, PART * LAYER);
        mask_c = mask_c | (mask_c << 1);
        T connected = Mask & mask_c;
        while (true) {
            T last = connected;
            T connected_up = connected << (2 * PART);
            Shape s{connected};
            T connected_left_right = s.rotate(1).value | s.rotate(PART - 1).value;
            T connected_down = connected >> (2 * PART);
            connected |= mask_c & (connected_up | connected_left_right | connected_down);
            if (connected == last) {
                break;
            }
        }
        return Shape(value & ~connected);
    }

    // Cut the shape. Returns the west half
    constexpr Shape cut() const {
        // mask of the west half
        constexpr T mask = repeat<T>(repeat<T>(3, 2, PART / 2), 2 * PART,
                                     LAYER);
        // break all the crystals in the east half, and connected ones
        Shape ret = breakCrystals<~mask>();
        // remove everything in the east half
        ret.value &= mask;
        // apply gravity
        return ret.collapse();
    }

    // Apply pin pusher
    constexpr Shape pin() const {
        // Find the places to add pins
        T empty = find<Type::Empty>();
        T pins = ~empty & repeat<T>(T(Type::Pin), 2, PART);
        // Break crystals on the top layer
        constexpr T top = repeat<T>(3, 2, PART) << (2 * PART * (LAYER - 1));
        Shape ret = breakCrystals<top>();
        // Push, and apply gravity
        return Shape((ret.value << (2 * PART)) | pins).collapse();
    }

    // Mirror the shape
    constexpr Shape flip() const {
        if constexpr (PART == 4) {
            constexpr int D = (PART/2-1)*2;
            T ma = repeat<T>(3, 2*PART/2, 2*LAYER);
            T mb = repeat<T>(3<<D, 2*PART/2, 2*LAYER);
            T v = ((value & ma) << D) | ((value & mb) >> D);
            return Shape(v);
        } else {
            T v = 0;
            for (size_t pa = 0; pa < PART / 2; ++pa) {
                size_t pb = PART - 1 - pa;
                T ma = repeat<T>(3, 2 * PART, LAYER) << (pa * 2);
                T mb = repeat<T>(3, 2 * PART, LAYER) << (pb * 2);
                v |= (value & ma) << (pb * 2 - pa * 2);
                v |= (value & mb) >> (pb * 2 - pa * 2);
            }
            return Shape(v);
        }
    }

    // Normalize pins as the vortex does
    constexpr Shape normalize() const {
        T filled = find<Type::Shape>() | find<Type::Crystal>();
        T pin = find<Type::Pin>();
        T keepPin = pin;
        for (; filled; filled >>= 2 * PART) {
            keepPin &= ~filled;
        }
        return Shape(value & ~(pin & ~keepPin));
    }

    // All the shapes that can be obtained by rotation and flip
    std::vector<Shape> equivalentShapes() const {
        std::vector<Shape> ret;
        for (size_t angle = 0; angle < PART; ++angle) {
            ret.push_back(rotate(angle));
            ret.push_back(rotate(angle).flip());
        }
        std::sort(ret.begin(), ret.end());
        auto it = std::unique(ret.begin(), ret.end());
        ret.resize(it - ret.begin());
        return ret;
    }

    Shape equivalentShape() const {
        std::vector<Shape> ret;
        for (size_t angle = 0; angle < PART; ++angle) {
            ret.push_back(rotate(angle));
            ret.push_back(rotate(angle).flip());
        }
        return *std::min_element(ret.begin(), ret.end());
    }

    // All the halves that can be obtained by flip
    std::vector<Shape> equivalentHalves() const {
        Shape flipped = flip();
        if (flipped < *this) {
            return {flipped, *this};
        } else if (*this < flipped) {
            return {*this, flipped};
        } else {
            return {*this};
        }
    }

    Shape equivalentHalve() const {
        Shape flipped = flip();
        if (flipped < *this) {
            return flipped;
        } else {
            return *this;
        }
    }
};

struct ShapeSet {
    std::vector<Shape> halves;
    std::vector<Shape> shapes;

    void save(const std::string& filename) const {
        using namespace std;
        ofstream file{filename, ios::out | ios::binary | ios::trunc};
        uint32_t size = halves.size();
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        file.write(reinterpret_cast<const char*>(halves.data()),
                   size * sizeof(Shape));
        size = shapes.size();
        file.write(reinterpret_cast<const char*>(&size), sizeof(size));
        file.write(reinterpret_cast<const char*>(shapes.data()),
                   size * sizeof(Shape));
    }

    static ShapeSet load(const std::string& filename) {
        using namespace std;
        ShapeSet ret;
        ifstream file{filename, ios::in | ios::binary};
        uint32_t size;
        file.read(reinterpret_cast<char*>(&size), sizeof(size));
        ret.halves.resize(size);
        file.read(reinterpret_cast<char*>(ret.halves.data()),
                  size * sizeof(Shape));
        file.read(reinterpret_cast<char*>(&size), sizeof(size));
        ret.shapes.resize(size);
        file.read(reinterpret_cast<char*>(ret.shapes.data()),
                  size * sizeof(Shape));
        return ret;
    }
};

}

template <>
struct std::hash<Shapez::Shape> {
    std::size_t operator()(const Shapez::Shape& shape) const noexcept {
        return shape.value;
    }
};

#pragma once

#include <theta-hierarchy/defs.h>

namespace theta {

/**
 * @brief A fractional index with strong ordering.
 *
 * This is an infinite precision number between 0 and 1 exclusive. It can be
 * thought of as a base 256 representation with unlimited digits right of the decimal.
 * The leading zero is not stored.
 */
struct Position {
    using symbol_t = uint8_t;

private:

    static constexpr size_t   SYMBOL_BITS = sizeof(symbol_t) * 8;
    static constexpr symbol_t HIGH_BIT    =  ((symbol_t) 1) << (SYMBOL_BITS - 1);
    static constexpr symbol_t FULL_SYMBOL = ~((symbol_t) 0);

    // number of locally-stored symbols. chosen so that the whole struct
    // is a multiple of 8 bytes, for alignment.
    static constexpr size_t K = 14;

    template <typename T, typename H>
    friend struct geom::Digest;

    // high order bits are at low indices.
    union {
        symbol_t  _symbols[K];
        symbol_t* _symbol_ptr;
    };
    uint16_t _size;

    Position(size_t n);

    void _push_back(symbol_t s);

protected:

          symbol_t* _data();
    const symbol_t* _data() const;

    symbol_t operator[](size_t i) const;

public:

    Position();
    Position(const Position&  other);
    Position(      Position&& other);
    ~Position();

    Position& operator=(const Position&  other);
    Position& operator=(      Position&& other);

    std::strong_ordering operator<=>(const Position& other) const;
    bool operator==(const Position& other) const {
        return (*this <=> other) == std::strong_ordering::equal;
    }

    Position before() const;
    Position between(const Position& other) const;
    Position after()  const;
};

} // namespace theta


template <typename H>
struct geom::Digest<theta::Position,H> {
    H operator()(const theta::Position& pos) const {
        using symbol_t = theta::Position::symbol_t;
        H nonce = geom::truncated_constant<H>(0x249707f545e427faULL, 0x5905299ebf487d2b);
        return geom::hash_bytes<H>(
            nonce,
            pos._data(),
            pos._size * sizeof(symbol_t)
        );
    }
};


template <>
struct std::hash<theta::Position> {
    size_t operator()(const theta::Position& pos) const;
};

/**
 * @file position.h
 * @brief Fractional-index type for ordering siblings in a hierarchy.
 */

#pragma once

#include <entttree/defs.h>

namespace entttree {

/**
 * @brief A fractional index with strong ordering, used for sibling positioning.
 *
 * This is an infinite-precision number between 0 and 1 exclusive, represented
 * as a base-256 value with unlimited digits right of the decimal point. The
 * leading zero is not stored.
 *
 * Chunking into 8-bit symbols balances two growth pressures: large symbols
 * reduce growth from sequential `before()`/`after()` calls (~128 steps before
 * a new symbol is needed), while the `between()` operation grows the number
 * only when the last symbols of the two positions are within 1 of each other.
 *
 * Up to 14 symbols (112 bits) are stored inline; beyond that, heap storage
 * is used. Default-constructed positions start at the midpoint (0.5).
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
        symbol_t  _symbols[K];  ///< Inline storage for small positions.
        symbol_t* _symbol_ptr;  ///< Heap pointer when size exceeds K.
    };
    uint16_t _size;  ///< Number of symbols in the representation.

    Position(size_t n);

    void _push_back(symbol_t s);

protected:

          symbol_t* _data();
    const symbol_t* _data() const;

    symbol_t operator[](size_t i) const;

public:

    /// Construct a Position at the midpoint (0.5).
    Position();
    Position(const Position&  other);
    Position(      Position&& other);
    ~Position();

    Position& operator=(const Position&  other);
    Position& operator=(      Position&& other);

    /// Lexicographic three-way comparison.
    std::strong_ordering operator<=>(const Position& other) const;
    bool operator==(const Position& other) const {
        return (*this <=> other) == std::strong_ordering::equal;
    }

    /// @brief Return a position immediately before this one.
    Position before() const;

    /**
     * @brief Return a position midway between this position and `other`.
     *
     * The result is always strictly between the two operands regardless
     * of their ordering. The representation may grow by one symbol when
     * the two positions are adjacent.
     */
    Position between(const Position& other) const;

    /// @brief Return a position immediately after this one.
    Position after()  const;
};

} // namespace entttree


template <typename H>
struct geom::Digest<entttree::Position,H> {
    H operator()(const entttree::Position& pos) const {
        using symbol_t = entttree::Position::symbol_t;
        H nonce = geom::truncated_constant<H>(0x249707f545e427faULL, 0x5905299ebf487d2b);
        return geom::hash_bytes<H>(
            nonce,
            pos._data(),
            pos._size * sizeof(symbol_t)
        );
    }
};


template <>
struct std::hash<entttree::Position> {
    size_t operator()(const entttree::Position& pos) const;
};

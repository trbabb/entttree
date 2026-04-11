#include <algorithm>
#include <compare>

#include <theta-hierarchy/position.h>

namespace theta {

//////// private helpers ////////

Position::symbol_t* Position::_data() {
    return _size > K ? _symbol_ptr : _symbols;
}

const Position::symbol_t* Position::_data() const {
    return _size > K ? _symbol_ptr : _symbols;
}

Position::symbol_t Position::operator[](size_t i) const {
    if (i >= _size) return 0;
    return _data()[i];
}

void Position::_push_back(symbol_t s) {
    if (_size < K) [[likely]] {
        _symbols[_size] = s;
    } else {
        symbol_t* cur_data = _data();
        symbol_t* new_ptr  = new symbol_t[_size + 1];
        std::copy(cur_data, cur_data + _size, new_ptr);
        new_ptr[_size] = s;
        if (_size > K) delete[] _symbol_ptr;
        _symbol_ptr = new_ptr;
    }
    ++_size;
}


//////// structors ////////

Position::Position(size_t n):
    _symbols{},
    _size(n)
{
    if (n > K) {
        _symbol_ptr = new symbol_t[n];
    }
}

Position::Position():Position(2) {
    _data()[1] = HIGH_BIT;
}

Position::Position(const Position& other):
        _size(other._size)
{
    symbol_t* data;
    if (_size > K) {
        _symbol_ptr = new symbol_t[_size];
        data = _symbol_ptr;
    } else {
        data = _symbols;
    }
    const symbol_t* other_data = other._data();
    std::copy(other_data, other_data + _size, data);
}

Position::Position(Position&& other):
        _size(other._size)
{
    if (_size > K) {
        _symbol_ptr = other._symbol_ptr;
        other._size = 0;
    } else {
        const symbol_t* other_data = other._data();
        std::copy(other_data, other_data + _size, _symbols);
    }
}

Position::~Position() {
    if (_size > K) {
        delete[] _symbol_ptr;
    }
}


//////// assignment ////////

Position& Position::operator=(const Position& other) {
    if (this == &other) return *this;
    if (_size > K) {
        delete[] _symbol_ptr;
    }
    _size = other._size;
    const symbol_t* other_data = other._data();
    symbol_t* data = _symbols;
    if (_size > K) {
        _symbol_ptr = new symbol_t[_size];
        data = _symbol_ptr;
    }
    std::copy(other_data, other_data + _size, data);
    return *this;
}

Position& Position::operator=(Position&& other) {
    if (this == &other) return *this;
    if (_size > K) {
        delete[] _symbol_ptr;
    }
    _size = other._size;
    if (_size > K) {
        _symbol_ptr = other._symbol_ptr;
        other._size = 0;
    } else {
        const symbol_t* other_data = other._symbols;
        std::copy(other_data, other_data + _size, _symbols);
    }
    return *this;
}


//////// comparison ////////

std::strong_ordering Position::operator<=>(const Position& other) const {
    size_t n = std::min(_size, other._size);
    const symbol_t* data_a =       _data();
    const symbol_t* data_b = other._data();
    for (size_t i = 0; i < n; ++i) {
        if (data_a[i] < data_b[i]) {
            return std::strong_ordering::less;
        } else if (data_a[i] > data_b[i]) {
            return std::strong_ordering::greater;
        }
    }
    return _size <=> other._size;
}


//////// methods ////////

Position Position::after() const {
    Position result {_size};
    uint8_t carry = 1;
    bool all_zero = true;
    symbol_t* result_data = result._data();
    const symbol_t* data = _data();
    for (int i = int(_size) - 1; i >= 0; --i) {
        result_data[i] = data[i] + carry;
        carry = result_data[i] < data[i];
        all_zero = all_zero and (result_data[i] == 0);
    }
    if (all_zero) [[unlikely]] {
        for (size_t i = 0; i < _size; ++i) {
            result_data[i] = FULL_SYMBOL;
        }
        result._push_back(HIGH_BIT);
    }
    return result;
}

Position Position::before() const {
    Position result {_size};
    symbol_t* result_data = result._data();
    const symbol_t*  data = _data();

    uint8_t borrow = 1;
    bool  all_zero = true;
    for (int i = int(_size) - 1; i >= 0; --i) {
        result_data[i] = data[i] - borrow;
        borrow   = result_data[i] > data[i];
        all_zero = all_zero and (result_data[i] == 0);
    }
    if (all_zero) [[unlikely]] {
        result._push_back(HIGH_BIT);
    }
    return result;
}

Position Position::between(const Position& other) const {
    const symbol_t* data_a =       _data();
    const symbol_t* data_b = other._data();
    size_t sz_a =       _size;
    size_t sz_b = other._size;
    size_t sz_x = std::max(sz_a, sz_b);

    symbol_t last_a = (sz_x <= sz_a) ? data_a[sz_x - 1] : 0;
    symbol_t last_b = (sz_x <= sz_b) ? data_b[sz_x - 1] : 0;
    static_assert(std::is_unsigned_v<symbol_t>, "assumption: symbol_t nonnegative");
    if (last_a - last_b <= 1 or last_b - last_a <= 1) ++sz_x;

    Position result {sz_x};
    symbol_t* result_data = result._data();
    symbol_t carry = 0;
    for (int i = int(result._size) - 1; i >= 0; --i) {
        symbol_t a = (i < (int) sz_a ? data_a[i] : 0) >> 1;
        symbol_t b = (i < (int) sz_b ? data_b[i] : 0) >> 1;
        if (i > 0) {
            if (i <= (int) sz_a) a |= (data_a[i - 1] & 1) << (SYMBOL_BITS - 1);
            if (i <= (int) sz_b) b |= (data_b[i - 1] & 1) << (SYMBOL_BITS - 1);
        }
        symbol_t sum = a + b + carry;
        result_data[i] = sum;
        carry = sum < a;
    }

    return result;
}

} // namespace theta


size_t std::hash<theta::Position>::operator()(const theta::Position& pos) const {
    return geom::hash<theta::Position,size_t>(pos);
}

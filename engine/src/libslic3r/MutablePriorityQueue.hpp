#ifndef slic3r_MutablePriorityQueue_hpp_
#define slic3r_MutablePriorityQueue_hpp_

#include <assert.h>
#include <cstddef>
#include <limits>
#include <type_traits>
#include <utility>
#include <vector>
constexpr auto InvalidQueueID = std::numeric_limits<size_t>::max();
template<typename T, typename IndexSetter, typename LessPredicate, const bool ResetIndexWhenRemoved = false>
class MutablePriorityQueue
{
public:
	static_assert(std::is_trivially_copyable<T>::value, "Template argument T must be a trivially copiable type in class template MutablePriorityQueue");

	MutablePriorityQueue(IndexSetter &&index_setter, LessPredicate &&less_predicate) :
		m_index_setter(std::forward<IndexSetter>(index_setter)), 
		m_less_predicate(std::forward<LessPredicate>(less_predicate)) 
		{}
	~MutablePriorityQueue()	{ clear(); }

	void		clear();
	void		reserve(size_t cnt) 				{ m_heap.reserve(cnt); }
	void		push(const T &item);
	void		push(T &&item);
	void		pop();
	T&			top()								{ return m_heap.front(); }
	void		remove(size_t idx);
	void		update(size_t idx) 					{ T item = m_heap[idx]; remove(idx); push(item); }

	size_t		size() const						{ return m_heap.size(); }
	bool		empty() const						{ return m_heap.empty(); }
	T&			operator[](std::size_t idx) noexcept { return m_heap[idx]; }
	const T&	operator[](std::size_t idx) const noexcept { return m_heap[idx]; }

	using iterator		 = typename std::vector<T>::iterator;
	using const_iterator = typename std::vector<T>::const_iterator;
	iterator 		begin() 		{ return m_heap.begin(); }
	iterator 		end() 			{ return m_heap.end(); }
	const_iterator 	cbegin() const	{ return m_heap.cbegin(); }
	const_iterator 	cend() const	{ return m_heap.cend(); }

protected:
	void		update_heap_up(size_t top, size_t bottom);
	void		update_heap_down(size_t top, size_t bottom);

private:
	std::vector<T>	m_heap;
	IndexSetter		m_index_setter;
	LessPredicate	m_less_predicate;
};

template<typename T, const bool ResetIndexWhenRemoved, typename IndexSetter, typename LessPredicate>
MutablePriorityQueue<T, IndexSetter, LessPredicate, ResetIndexWhenRemoved> make_mutable_priority_queue(IndexSetter &&index_setter, LessPredicate &&less_predicate)
{
    return MutablePriorityQueue<T, IndexSetter, LessPredicate, ResetIndexWhenRemoved>(
    	std::forward<IndexSetter>(index_setter), std::forward<LessPredicate>(less_predicate));
}

template<class T, class LessPredicate, class IndexSetter, const bool ResetIndexWhenRemoved>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter, ResetIndexWhenRemoved>::clear()
{ 
#ifdef NDEBUG
	if (ResetIndexWhenRemoved)
#endif
	{
		for (size_t idx = 0; idx < m_heap.size(); ++ idx)
			m_index_setter(m_heap[idx], std::numeric_limits<size_t>::max());
	}
	m_heap.clear();
}

template<class T, class LessPredicate, class IndexSetter, const bool ResetIndexWhenRemoved>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter, ResetIndexWhenRemoved>::push(const T &item)
{
	size_t idx = m_heap.size();
	m_heap.emplace_back(item);
	m_index_setter(m_heap.back(), idx);
	update_heap_up(0, idx);
}

template<class T, class LessPredicate, class IndexSetter, const bool ResetIndexWhenRemoved>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter, ResetIndexWhenRemoved>::push(T &&item)
{
	size_t idx = m_heap.size();
	m_heap.emplace_back(std::move(item));
	m_index_setter(m_heap.back(), idx);
	update_heap_up(0, idx);
}

template<class T, class LessPredicate, class IndexSetter, const bool ResetIndexWhenRemoved>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter, ResetIndexWhenRemoved>::pop()
{
	assert(! m_heap.empty());
#ifdef NDEBUG
	if (ResetIndexWhenRemoved)
#endif
	{
		m_index_setter(m_heap.front(), std::numeric_limits<size_t>::max());
	}
	if (m_heap.size() > 1) {
		m_heap.front() = m_heap.back();
		m_heap.pop_back();
		m_index_setter(m_heap.front(), 0);
		update_heap_down(0, m_heap.size() - 1);
	} else
		m_heap.clear();
}

template<class T, class LessPredicate, class IndexSetter, const bool ResetIndexWhenRemoved>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter, ResetIndexWhenRemoved>::remove(size_t idx)
{
	assert(idx < m_heap.size());
#ifdef NDEBUG
	if (ResetIndexWhenRemoved)
#endif
	{
		m_index_setter(m_heap[idx], std::numeric_limits<size_t>::max());
	}
	if (idx + 1 == m_heap.size()) {
		m_heap.pop_back();
		return;
	}
	m_heap[idx] = m_heap.back();
	m_index_setter(m_heap[idx], idx);
	m_heap.pop_back();
	update_heap_down(idx, m_heap.size() - 1);
	update_heap_up(0, idx);
}

template<class T, class LessPredicate, class IndexSetter, const bool ResetIndexWhenRemoved>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter, ResetIndexWhenRemoved>::update_heap_up(size_t top, size_t bottom)
{
	size_t childIdx = bottom;
	T *child = &m_heap[childIdx];
	for (;;) {
		size_t parentIdx = (childIdx - 1) >> 1;
		if (childIdx == 0 || parentIdx < top)
			break;
		T *parent = &m_heap[parentIdx];
		if (! m_less_predicate(*parent, *child)) {
			T tmp = *parent;
			m_index_setter(tmp,    childIdx);
			m_index_setter(*child, parentIdx);
			m_heap[parentIdx] = *child;
			m_heap[childIdx]  = tmp;
		}
		childIdx = parentIdx;
		child = parent;
	}
}

template<class T, class LessPredicate, class IndexSetter, const bool ResetIndexWhenRemoved>
inline void MutablePriorityQueue<T, LessPredicate, IndexSetter, ResetIndexWhenRemoved>::update_heap_down(size_t top, size_t bottom)
{
	size_t parentIdx = top;
	T *parent = &m_heap[parentIdx];
	for (;;) {
		size_t childIdx = (parentIdx << 1) + 1;
		if (childIdx > bottom)
			break;
		T *child = &m_heap[childIdx];
		size_t child2Idx = childIdx + 1;
		if (child2Idx <= bottom) {
			T *child2 = &m_heap[child2Idx];
			if (! m_less_predicate(*child, *child2)) {
				child = child2;
				childIdx = child2Idx;
			}
		}
		if (m_less_predicate(*parent, *child))
			return;
		T tmp = *parent;
		m_index_setter(tmp,    childIdx);
		m_index_setter(*child, parentIdx);
		m_heap[parentIdx] = *child;
		m_heap[childIdx] = tmp;
		parentIdx = childIdx;
		parent = child;
	}
}

// original source Copyright Björn Fahller 2015, Boost Software License, Version 1.0, http://www.boost.org/LICENSE_1_0.txt
template <std::size_t blocking>
struct SkipHeapAddressing
{
public:
	static const constexpr std::size_t block_size = blocking;
	static const constexpr std::size_t block_mask = block_size - 1;
	static_assert((block_size & block_mask) == 0U, "block size must be 2^n for some integer n");

	static inline std::size_t child_of(std::size_t node_no) noexcept {
  		if (! is_block_leaf(node_no))
			return node_no + block_offset(node_no);
		return (block_base(node_no) + 1 + child_no(node_no) * 2) * block_size + 1;
	}

	static inline std::size_t parent_of(std::size_t node_no) noexcept {
  		auto const node_root = block_base(node_no);
  		if (! is_block_root(node_no))
			return node_root + block_offset(node_no) / 2;
		auto const parent_base = block_base(node_root / block_size - 1);
		auto const child       = ((node_no - block_size) / block_size - parent_base) / 2;
		return 
			parent_base + 
			block_size / 2 + child;
	}

	static inline bool 			is_block_leaf(std::size_t node_no) noexcept { return (node_no & (block_size >> 1)) != 0U; }
	static inline bool 			is_padding   (std::size_t node_no) noexcept { return block_offset(node_no) == 0U; }
	static inline bool 			is_block_root(std::size_t node_no) noexcept { return block_offset(node_no) == 1U; }
	static inline std::size_t 	block_offset (std::size_t node_no) noexcept { return node_no & block_mask; }
	static inline std::size_t 	block_base   (std::size_t node_no) noexcept { return node_no & ~block_mask; }
	static inline std::size_t 	child_no     (std::size_t node_no) noexcept { assert(is_block_leaf(node_no)); return node_no & (block_mask >> 1); }
};

template<typename T, typename IndexSetter, typename LessPredicate, std::size_t blocking = 32, const bool ResetIndexWhenRemoved = false>
class MutableSkipHeapPriorityQueue
{
public:
	static_assert(std::is_trivially_copyable<T>::value, "Template argument T must be a trivially copiable type in class template MutableSkipHeapPriorityQueue");
	using address = SkipHeapAddressing<blocking>;

	MutableSkipHeapPriorityQueue(IndexSetter &&index_setter, LessPredicate &&less_predicate) :
		m_index_setter(std::forward<IndexSetter>(index_setter)), 
		m_less_predicate(std::forward<LessPredicate>(less_predicate)) 
		{}
	~MutableSkipHeapPriorityQueue()	{ clear(); }

	void		clear();
	void		reserve(size_t cnt) 				{ m_heap.reserve(cnt + ((cnt + (address::block_size - 1)) / (address::block_size - 1))); }
	void		push(const T &item);
	void		push(T &&item);
	void		pop();
	T&			top()								{ return m_heap[1]; }
	void		remove(size_t idx);
	void		update(size_t idx) 					{ assert(! address::is_padding(idx)); T item = m_heap[idx]; remove(idx); push(item); }
	size_t 		size() const noexcept 				{ return m_heap.size() - (m_heap.size() + address::block_size - 1) / address::block_size; }
	bool		empty() const						{ return m_heap.empty(); }
	T&			operator[](std::size_t idx) noexcept { assert(! address::is_padding(idx)); return m_heap[idx]; }
	const T&    operator[](std::size_t idx) const noexcept { assert(! address::is_padding(idx)); return m_heap[idx]; }

protected:
	void		update_heap_up(size_t top, size_t bottom);
	void		update_heap_down(size_t top, size_t bottom);
	void   		pop_back() noexcept {
		assert(m_heap.size() > 1);
		assert(! address::is_padding(m_heap.size() - 1));
		m_heap.pop_back();
		if (address::is_padding(m_heap.size() - 1))
			m_heap.pop_back();
	}

private:
	std::vector<T>	m_heap;
	IndexSetter		m_index_setter;
	LessPredicate	m_less_predicate;
};

template<typename T, std::size_t BlockSize, const bool ResetIndexWhenRemoved, typename IndexSetter, typename LessPredicate>
MutableSkipHeapPriorityQueue<T, IndexSetter, LessPredicate, BlockSize, ResetIndexWhenRemoved> 
	make_miniheap_mutable_priority_queue(IndexSetter &&index_setter, LessPredicate &&less_predicate)
{
    return MutableSkipHeapPriorityQueue<T, IndexSetter, LessPredicate, BlockSize, ResetIndexWhenRemoved>(
    	std::forward<IndexSetter>(index_setter), std::forward<LessPredicate>(less_predicate));
}

template<class T, class LessPredicate, class IndexSetter, std::size_t blocking, const bool ResetIndexWhenRemoved>
inline void MutableSkipHeapPriorityQueue<T, LessPredicate, IndexSetter, blocking, ResetIndexWhenRemoved>::clear()
{ 
#ifdef NDEBUG
	if (ResetIndexWhenRemoved)
#endif
	{
		for (size_t idx = 0; idx < m_heap.size(); ++ idx)
			if (! address::is_padding(idx))
				m_index_setter(m_heap[idx], std::numeric_limits<size_t>::max());
	}
	m_heap.clear();
}

template<class T, class LessPredicate, class IndexSetter, std::size_t blocking, const bool ResetIndexWhenRemoved>
inline void MutableSkipHeapPriorityQueue<T, LessPredicate, IndexSetter, blocking, ResetIndexWhenRemoved>::push(const T &item)
{
	if (address::is_padding(m_heap.size()))
		m_heap.emplace_back(T());
	size_t idx = m_heap.size();
	m_heap.emplace_back(item);
	m_index_setter(m_heap.back(), idx);
	update_heap_up(1, idx);
}

template<class T, class LessPredicate, class IndexSetter, std::size_t blocking, const bool ResetIndexWhenRemoved>
inline void MutableSkipHeapPriorityQueue<T, LessPredicate, IndexSetter, blocking, ResetIndexWhenRemoved>::push(T &&item)
{
	if (address::is_padding(m_heap.size()))
		m_heap.emplace_back(T());
	size_t idx = m_heap.size();
	m_heap.emplace_back(std::move(item));
	m_index_setter(m_heap.back(), idx);
	update_heap_up(1, idx);
}

template<class T, class LessPredicate, class IndexSetter, std::size_t blocking, const bool ResetIndexWhenRemoved>
inline void MutableSkipHeapPriorityQueue<T, LessPredicate, IndexSetter, blocking, ResetIndexWhenRemoved>::pop()
{
	assert(! m_heap.empty());
#ifdef NDEBUG
	if (ResetIndexWhenRemoved)
#endif
	{
		m_index_setter(m_heap[1], std::numeric_limits<size_t>::max());
	}
	if (m_heap.size() > 2) {
		m_heap[1] = m_heap.back();
		this->pop_back();
		m_index_setter(m_heap[1], 1);
		update_heap_down(1, m_heap.size() - 1);
	} else
		m_heap.clear();
}

template<class T, class LessPredicate, class IndexSetter, std::size_t blocking, const bool ResetIndexWhenRemoved>
inline void MutableSkipHeapPriorityQueue<T, LessPredicate, IndexSetter, blocking, ResetIndexWhenRemoved>::remove(size_t idx)
{
	assert(idx < m_heap.size());
	assert(! address::is_padding(idx));
#ifdef NDEBUG
	if (ResetIndexWhenRemoved)
#endif
	{
		m_index_setter(m_heap[idx], std::numeric_limits<size_t>::max());
	}
	if (idx + 1 == m_heap.size()) {
		this->pop_back();
		return;
	}
	m_heap[idx] = m_heap.back();
	m_index_setter(m_heap[idx], idx);
	this->pop_back();
	update_heap_down(idx, m_heap.size() - 1);
	update_heap_up(1, idx);
}

template<class T, class LessPredicate, class IndexSetter, std::size_t blocking, const bool ResetIndexWhenRemoved>
inline void MutableSkipHeapPriorityQueue<T, LessPredicate, IndexSetter, blocking, ResetIndexWhenRemoved>::update_heap_up(size_t top, size_t bottom)
{
	assert(! address::is_padding(top));
	assert(! address::is_padding(bottom));
	size_t childIdx = bottom;
	T *child = &m_heap[childIdx];
	for (;;) {
		size_t parentIdx = address::parent_of(childIdx);
		if (childIdx == 1 || parentIdx < top)
			break;
		T *parent = &m_heap[parentIdx];
		if (! m_less_predicate(*parent, *child)) {
			T tmp = *parent;
			m_index_setter(tmp,    childIdx);
			m_index_setter(*child, parentIdx);
			m_heap[parentIdx] = *child;
			m_heap[childIdx]  = tmp;
		}
		childIdx = parentIdx;
		child = parent;
	}
}

template<class T, class LessPredicate, class IndexSetter, std::size_t blocking, const bool ResetIndexWhenRemoved>
inline void MutableSkipHeapPriorityQueue<T, LessPredicate, IndexSetter, blocking, ResetIndexWhenRemoved>::update_heap_down(size_t top, size_t bottom)
{
	assert(! address::is_padding(top));
	assert(! address::is_padding(bottom));
	size_t parentIdx = top;
	T *parent = &m_heap[parentIdx];
	for (;;) {
		size_t childIdx = address::child_of(parentIdx);
		if (childIdx > bottom)
			break;
		T *child = &m_heap[childIdx];
		size_t child2Idx = childIdx + (address::is_block_leaf(parentIdx) ? address::block_size : 1);
		if (child2Idx <= bottom) {
			T *child2 = &m_heap[child2Idx];
			if (! m_less_predicate(*child, *child2)) {
				child = child2;
				childIdx = child2Idx;
			}
		}
		if (m_less_predicate(*parent, *child))
			return;
		T tmp = *parent;
		m_index_setter(tmp,    childIdx);
		m_index_setter(*child, parentIdx);
		m_heap[parentIdx] = *child;
		m_heap[childIdx]  = tmp;
		parentIdx = childIdx;
		parent = child;
	}
}

#endif

#ifndef slic3r_ObjectID_hpp_
#define slic3r_ObjectID_hpp_

#include <cereal/access.hpp>
#include <cereal/types/base_class.hpp>
#include <cstddef>
#include <cstdint>

namespace Slic3r {

namespace UndoRedo {
	class StackImpl;
};

class ObjectID
{
public:
	ObjectID(size_t id) : id(id) {}
	ObjectID() : id(0) {}

	bool operator==(const ObjectID &rhs) const { return this->id == rhs.id; }
	bool operator!=(const ObjectID &rhs) const { return this->id != rhs.id; }
	bool operator< (const ObjectID &rhs) const { return this->id <  rhs.id; }
	bool operator> (const ObjectID &rhs) const { return this->id >  rhs.id; }
	bool operator<=(const ObjectID &rhs) const { return this->id <= rhs.id; }
	bool operator>=(const ObjectID &rhs) const { return this->id >= rhs.id; }

    bool valid() const { return id != 0; }
    bool invalid() const { return id == 0; }

	size_t	id;

private:
	friend class cereal::access;
	template<class Archive> void serialize(Archive &ar) { ar(id); }
};

struct ObjectInstanceID {
    ObjectID object_id;
    size_t   instance_id { size_t(-1) };

    bool operator==(const ObjectInstanceID& rhs) const { return object_id == rhs.object_id && instance_id == rhs.instance_id; }
    bool operator!=(const ObjectInstanceID& rhs) const { return !(*this == rhs); }
    bool operator<(const ObjectInstanceID& rhs) const
    {
        return object_id < rhs.object_id || (object_id == rhs.object_id && instance_id < rhs.instance_id);
    }
};

class ObjectBase
{
public:
    using Timestamp = uint64_t;

    ObjectID     		id() const { return m_id; }
    virtual Timestamp	timestamp() const { return 0; }

protected:
    ObjectBase() : m_id(generate_new_id()) {}
    ObjectBase(int) : m_id(ObjectID(0)) {}

    ObjectBase(const ObjectID id) : m_id(id) {}
	virtual ~ObjectBase() = default;

    void        set_new_unique_id() { m_id = generate_new_id(); }
    void        set_invalid_id()    { m_id = 0; }
    void        copy_id(const ObjectBase &rhs) { m_id = rhs.id(); }

    virtual void assign_new_unique_ids_recursive() { this->set_new_unique_id(); }

private:
    ObjectID                m_id;

	static inline ObjectID  generate_new_id() { return ObjectID(++ s_last_id); }
    static size_t           s_last_id;
	
	friend ObjectID wipe_tower_object_id();
	friend ObjectID wipe_tower_instance_id();

	friend class cereal::access;
	friend class Slic3r::UndoRedo::StackImpl;
	template<class Archive> void serialize(Archive &ar) { ar(m_id); }
  	template<class Archive> static void load_and_construct(Archive & ar, cereal::construct<ObjectBase> &construct) { ObjectID id; ar(id); construct(id); }
};

class ObjectWithTimestamp : public ObjectBase
{
protected:
	ObjectWithTimestamp() = default;
    ObjectWithTimestamp(int) : ObjectBase(-1) {}
	virtual ~ObjectWithTimestamp() = default;

    void                copy_timestamp(const ObjectWithTimestamp& rhs) { m_timestamp = rhs.m_timestamp; }

public:
    Timestamp	        timestamp() const throw() override { return m_timestamp; }
    bool 				timestamp_matches(const ObjectWithTimestamp &rhs) const throw() { return m_timestamp == rhs.m_timestamp; }
    bool 				object_id_and_timestamp_match(const ObjectWithTimestamp &rhs) const throw() { return this->id() == rhs.id() && m_timestamp == rhs.m_timestamp; }
    void 				touch() { m_timestamp = ++ s_last_timestamp; }

private:
	Timestamp 			m_timestamp { 1 };
    static Timestamp    s_last_timestamp;
	
	friend class cereal::access;
	friend class Slic3r::UndoRedo::StackImpl;
	template<class Archive> void serialize(Archive &ar) { ar(m_timestamp); }
};

class CutObjectBase : public ObjectBase
{
    size_t m_check_sum{1};
    size_t m_connectors_cnt{0};

public:
    CutObjectBase() : ObjectBase(-1) {}
    CutObjectBase(int) : ObjectBase(-1) {}
    CutObjectBase(ObjectID id, size_t check_sum, size_t connectors_cnt) : ObjectBase(id), m_check_sum(check_sum), m_connectors_cnt(connectors_cnt) {}
    virtual ~CutObjectBase() = default;

    bool operator<(const CutObjectBase &other) const { return other.id() > this->id(); }
    bool operator==(const CutObjectBase &other) const { return other.id() == this->id(); }

    void copy(const CutObjectBase &rhs)
    {
        this->copy_id(rhs);
        this->m_check_sum      = rhs.check_sum();
        this->m_connectors_cnt = rhs.connectors_cnt();
    }
    CutObjectBase(const CutObjectBase &) = default;

    CutObjectBase &operator=(const CutObjectBase &other)
    {
        this->copy(other);
        return *this;
    }

    void invalidate()
    {
        set_invalid_id();
        m_check_sum      = 1;
        m_connectors_cnt = 0;
    }

    void init() { this->set_new_unique_id(); }
    bool has_same_id(const CutObjectBase &rhs) { return this->id() == rhs.id(); }
    bool is_equal(const CutObjectBase &rhs) { return this->id() == rhs.id() && this->check_sum() == rhs.check_sum() && this->connectors_cnt() == rhs.connectors_cnt(); }

    size_t check_sum() const { return m_check_sum; }
    void   set_check_sum(size_t cs) { m_check_sum = cs; }
    void   increase_check_sum(size_t cnt) { m_check_sum += cnt; }

    size_t connectors_cnt() const { return m_connectors_cnt; }
    void   increase_connectors_cnt(size_t connectors_cnt) { m_connectors_cnt += connectors_cnt; }

private:
    friend class cereal::access;
    template<class Archive> void serialize(Archive &ar)
    {
        ar(cereal::base_class<ObjectBase>(this));
        ar(m_check_sum, m_connectors_cnt);
    }
};


extern ObjectID wipe_tower_object_id();
extern ObjectID wipe_tower_instance_id();

}

#endif

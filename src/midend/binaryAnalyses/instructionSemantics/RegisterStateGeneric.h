#ifndef ROSE_BinaryAnalysis_InstructionSemantics2_RegisterStateGeneric_H
#define ROSE_BinaryAnalysis_InstructionSemantics2_RegisterStateGeneric_H

#include <BaseSemantics2.h>

namespace rose {
namespace BinaryAnalysis {
namespace InstructionSemantics2 {
namespace BaseSemantics {

/** Smart pointer to a RegisterStateGeneric object.  RegisterStateGeneric objects are reference counted and should not be
 *  explicitly deleted. */
typedef boost::shared_ptr<class RegisterStateGeneric> RegisterStateGenericPtr;

/** A RegisterState for any architecture.
 *
 *  This state stores a list of non-overlapping registers and their values, typically only for the registers that have been
 *  accessed.  The state automatically switches between different representations when accessing a register that overlaps with
 *  one or more stored registers.  For instance, if the state stores 64-bit registers and the specimen suddently switches to
 *  32-bit mode, this state will split the 64-bit registers into 32-bit pieces.  If the analysis later returns to 64-bit mode,
 *  the 32-bit pieces are concatenated back to 64-bit values. This splitting and concatenation occurs on a per-register basis
 *  at the time the register is read or written.
 *
 *  The register state also stores optional information about writers for each register. Writer information (addresses of
 *  instructions that wrote to the register) are stored as sets defined at each bit of the register. This allows a wide
 *  register, like x86 RAX, to be written to in parts by different instructions, like x86 AL.  The register state itself
 *  doesn't update this information automatically--it only provides the API by which a higher software layer can manipulate the
 *  information.  This design allows the writer data structure to alternatively be used for things other than addresses of
 *  writing instructions.  For instance, the @ref SymbolicSemantics::RiscOperators has a setting that enables tracking
 *  writers. */
class RegisterStateGeneric: public RegisterState {
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Basic Types
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** A range of bits indexes.
     *
     *  Represents of contiguous interval of bit indexes, such as all bits numbered zero through 15, inclusive. */
    typedef Sawyer::Container::Interval<size_t> BitRange;

    /** Register map keys.
     *
     *  This class stores a register major and minor pair that is suitable for using as a key in an std::map or similar. These
     *  objects are implicitly constructed from @ref RegisterDescriptor. */
    struct RegStore {
        unsigned majr, minr;
        RegStore(const RegisterDescriptor &d) // implicit
            : majr(d.get_major()), minr(d.get_minor()) {}
        bool operator<(const RegStore &other) const {
            return majr<other.majr || (majr==other.majr && minr<other.minr);
        }
    };


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Types for storing values
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** A register descriptor and its value. */
    struct RegPair {
        RegisterDescriptor desc;
        SValuePtr value;
        RegPair(const RegisterDescriptor &desc, const SValuePtr &value): desc(desc), value(value) {}
    };

    /** Vector of register/value pairs. */
    typedef std::vector<RegPair> RegPairs;

    /** Values for all registers. */
    typedef Map<RegStore, RegPairs> Registers;


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Types for Boolean properties
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Properties stored per bit of each register.
     *
     *  Each bit of each register has Boolean properties that can be set or cleared independently of other bits or
     *  properties. The register state stores these properties, but it's up to higher layers to manipulate the bits, typically
     *  the @c readRegister and @c writeRegister methods of the @ref RiscOperators subclasses. */
    enum RegisterProperty {
        READ,                                           /**< The register was read on behalf of an instruction. */
        WRITTEN,                                        /**< The register was written on behalf of an instruction. */
        INITIALIZED,                                    /**< The register was written without an instruction. This
                                                         *   typically happens during state initialization. */
        READ_BEFORE_WRITE,                              /**< The register was read without having the WRITTEN property. */
        READ_UNINITIALIZED,                             /**< The register was read without having the WRITTEN or INITIALIZED
                                                         *   property. */
    };

    /** Set of Boolean properties.
     *
     *  If a property is present in the set then it is assumed to be true, otherwise the property is false. */
    typedef Sawyer::Container::Set<RegisterProperty> PropertySet;

    /** Boolean properties per bit.
     *
     *  This container stores properties per bit of a major/minor register pair.  For instance, the x86 16-bit AX register
     *  might have different sets of properties for its different subregisters, AL and AH.  This container stores those sets
     *  per bit. */
    typedef Sawyer::Container::IntervalSetMap<BitRange, PropertySet> BitProperties;

    /** Boolean properties for all registers.
     *
     *  This container is indexed by register major/minor pair, then by a bit number, and stores a set of properties. */
    typedef Sawyer::Container::Map<RegStore, BitProperties> RegisterProperties;


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Types for storing addresses
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Set of virtual addresses. */
    typedef Sawyer::Container::Set<rose_addr_t> AddressSet;

    /** Virtual addresses per bit. */
    typedef Sawyer::Container::IntervalSetMap<BitRange, AddressSet> BitAddressSet;

    /** Virtual addresses for all registers. */
    typedef Sawyer::Container::Map<RegStore, BitAddressSet> RegisterAddressSet;


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Data members
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
private:
    RegisterProperties properties_;                     // Boolean properties for each bit of each register.
    RegisterAddressSet writers_;                        // Writing instruction address set for each bit of each register
    bool coalesceOnRead_;                               // Modify representations when reading registers?

protected:
    /** Values for registers that have been accessed.
     *
     *  This is a map whose keys are major/minor pairs and whose values are IntervalSetMaps that associate a value with
     *  non-overlapping ranges of bits. When reading or writing a register, the register being accessed is guaranteed to
     *  overlap only with those registers on the matching major-minor list, if it overlaps at all.  The lists are typically
     *  short (e.g., one list might refer to all the parts of the x86 RAX register, but the RBX parts would be on a different
     *  list. None of the registers stored on a particular list overlap with any other register on that same list; when adding
     *  new register that would overlap, the registers with which it overlaps must be removed first. */
    Registers registers;


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Normal constructors
    //
    // These are protected because objects of this class are reference counted and always allocated on the heap.
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
protected:
    explicit RegisterStateGeneric(const SValuePtr &protoval, const RegisterDictionary *regdict)
        : RegisterState(protoval, regdict), coalesceOnRead_(true) {
        clear();
    }

    RegisterStateGeneric(const RegisterStateGeneric &other)
        : RegisterState(other), properties_(other.properties_), writers_(other.writers_), coalesceOnRead_(true),
          registers(other.registers) {
        deep_copy_values();
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Static allocating constructors
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Instantiate a new register state. The @p protoval argument must be a non-null pointer to a semantic value which will be
     *  used only to create additional instances of the value via its virtual constructors.  The prototypical value is normally
     *  of the same type for all parts of a semantic analysis: its state and operator classes.
     *
     *  The register dictionary, @p regdict, describes the registers that can be stored by this register state, and should be
     *  compatible with the register dictionary used for other parts of binary analysis. */
    static RegisterStateGenericPtr instance(const SValuePtr &protoval, const RegisterDictionary *regdict) {
        return RegisterStateGenericPtr(new RegisterStateGeneric(protoval, regdict));
    }

    /** Instantiate a new copy of an existing register state. */
    static RegisterStateGenericPtr instance(const RegisterStateGenericPtr &other) {
        return RegisterStateGenericPtr(new RegisterStateGeneric(*other));
    }

    
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Virtual constructors
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    virtual RegisterStatePtr create(const SValuePtr &protoval, const RegisterDictionary *regdict) const ROSE_OVERRIDE {
        return instance(protoval, regdict);
    }

    virtual RegisterStatePtr clone() const ROSE_OVERRIDE {
        return RegisterStateGenericPtr(new RegisterStateGeneric(*this));
    }

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Dynamic pointer casts
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Run-time promotion of a base register state pointer to a RegisterStateGeneric pointer. This is a checked conversion--it
     *  will fail if @p from does not point to a RegisterStateGeneric object. */
    static RegisterStateGenericPtr promote(const RegisterStatePtr &from) {
        RegisterStateGenericPtr retval = boost::dynamic_pointer_cast<RegisterStateGeneric>(from);
        ASSERT_not_null(retval);
        return retval;
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Object properties
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Property: Whether reading modifies representation.
     *
     *  When the @ref readRegister method is called to obtain a value for a desired register that overlaps with some (parts of)
     *  registers that already exist in this register state we can proceed in two ways. In both cases the return value will
     *  include data that's already stored, but the difference is in how we store the returned value in the register state: (1)
     *  we can erase the (parts of) existing registers that overlap and store the desired register and store the returned value
     *  so that the register we just read appears as one atomic value, or (2) we can keep the existing registers and write only
     *  those parts of the return value that fall between the gaps.
     *
     *  If the coalesceOnRead property is set, then the returned value is stored atomically even when the value might be a
     *  function of values that are already stored. Otherwise, existing registerss are not rearranged and only those parts of
     *  the return value that fall into the gaps between existing registers are stored.
     *
     *  The set/clear modifiers return the previous value of this property.
     *
     * @{ */
    bool coalesceOnRead() const ROSE_FINAL { return coalesceOnRead_; }
    virtual void coalesceOnRead(bool b) { coalesceOnRead_ = b; }
    /** @} */

    /** Temporarily turn off coalescing on read.  Original state is restored by the destructor. */
    class NoCoalesceOnRead {
        RegisterStateGeneric *rstate_;
        bool oldValue_;
    public:
        /** Turn off coalesceOnRead for the specified register state. */
        explicit NoCoalesceOnRead(RegisterStateGeneric *rstate)
            : rstate_(rstate), oldValue_(rstate->coalesceOnRead()) {
            rstate->coalesceOnRead(false);
        }
        ~NoCoalesceOnRead() {
            rstate_->coalesceOnRead(oldValue_);
        }
    };

    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Inherited non-constructors
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    virtual void clear() ROSE_OVERRIDE;
    virtual void zero() ROSE_OVERRIDE;
    virtual SValuePtr readRegister(const RegisterDescriptor &reg, RiscOperators *ops) ROSE_OVERRIDE;
    virtual void writeRegister(const RegisterDescriptor &reg, const SValuePtr &value, RiscOperators *ops) ROSE_OVERRIDE;
    virtual void print(std::ostream&, Formatter&) const ROSE_OVERRIDE;
    virtual bool merge(const RegisterStatePtr &other, RiscOperators *ops) ROSE_OVERRIDE;


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Initialization
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Initialize all registers of the dictionary.  When the dictionary contains overlapping registers, only the largest
     *  registers are initialized. For example, on a 32-bit x86 architecture EAX would be initialized but not AX, AH, or AL;
     *  requesting AX, AH, or AL will return part of the initial EAX value. */
    virtual void initialize_large();

    /** Initialize all registers of the dictionary.  When the dictionary contains overlapping registers, only the smallest
     *  registers are initialized. For example, on a 32-bit x86 architecture, AX, AH, AL and the non-named high-order 16 bits
     *  of AX are inititialized, but EAX isn't explicitly initialized.  Requesting the value of EAX will return a value
     *  constructed from the various smaller parts. */
    virtual void initialize_small();

    /** Initialize the specified registers of the dictionary.  Each register in the list must not overlap with any other
     *  register in the list, or strange things will happen.  If @p initialize_to_zero is set then the specified registers are
     *  initialized to zero, otherwise they're initialized with the prototypical value's constructor that takes only a size
     *  parameter. This method is somewhat low level and doesn't do much error checking. */
    void initialize_nonoverlapping(const std::vector<RegisterDescriptor>&, bool initialize_to_zero);


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Value storage queries
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Returns the list of all registers and their values.  The returned registers are guaranteed to be non-overlapping,
     * although they might not correspond to actual named machine registers.  For instance, if a 32-bit value was written to
     * the x86 EFLAGS register then the return value will contain a register/value pair for EFLAGS but no pairs for individual
     * flags.  If one subsequently writes a 1-bit value to the ZF flag (bit 6 of EFLAGS) then the return value will contain a
     * register/value pair for ZF, and also a pair for bits 0-5, and a pair for bits 7-31, neither of which correspond to
     * actual register names in x86 (there is no name for bits 0-5 as a whole). The readRegister() and writeRegister() methods
     * can be used to re-cast the various pairs into other groupings; get_stored_registers() is a lower-level interface. */
    virtual RegPairs get_stored_registers() const;

    /** Determines if some of the specified register is stored in the state. Returns true even if only part of the requested
     *  register is in the state (as when one asks about EAX and the state only stores AX). This is slightly more efficient
     *  than calling stored_parts():
     *
     * @code
     *  RegisterStateGenericPtr rstate = ...;
     *  RegisterDescriptor reg = ...;
     *  assert(rstate->partly_exists(reg) == !parts_exist(reg).empty());
     * @endcode
     */
    virtual bool is_partly_stored(const RegisterDescriptor&) const;

    /** Determines if the specified register is wholly stored in the state. Returns if the state contains data for the entire
     *  register, even if that data is split among several smaller parts or exists as a subset of a larger part. */
    virtual bool is_wholly_stored(const RegisterDescriptor&) const;

    /** Determines if the specified register is stored exactly in the state. Returns true only if the specified register wholly
     *  exists and a value can be returned without extracting or concatenating values from larger or smaller stored parts. Note
     *  that a value can also be returned without extracting or conctenating if the state contains no data for the specified
     *  register, as indicated by is_partly_stored() returning false. */
    virtual bool is_exactly_stored(const RegisterDescriptor&) const;

    /** Returns a description of which bits of a register are stored.  The return value is an ExtentMap that contains the bits
     * that are stored in the state. This does not return the value of any parts of stored registers--one gets that with
     * readRegister(). The return value does not contain any bits that are not part of the specified register. */
    virtual ExtentMap stored_parts(const RegisterDescriptor&) const;

    /** Find stored registers overlapping with specified register.
     *
     *  Returns all stored registers that overlap with the specified register.  The registers in the returned vector will never
     *  overlap with each other, but they will all overlap with the specified register. */
    virtual RegPairs overlappingRegisters(const RegisterDescriptor&) const;

    /** Cause a register to not be stored.  Erases all record of the specified register. The RiscOperators pointer is used for
     *  its extract operation if the specified register is not exactly stored in the state, such as if the state
     *  stores RIP and one wants to erase only the 32-bits overlapping with EIP. */
    virtual void erase_register(const RegisterDescriptor&, RiscOperators*);


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Traversals
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Functors for traversing register values in a register state. */
    class Visitor {
    public:
        virtual ~Visitor() {}
        virtual SValuePtr operator()(const RegisterDescriptor&, const SValuePtr&) = 0;
    };

    /** Traverse register/value pairs.  Traverses all the (non-overlapping) registers and their values, calling the specified
     *  functor for each register/value pair. If the functor returns a new SValue then the return value becomes the new value
     *  for that register.  The new value must have the same width as the register.
     *
     *  For example, the following code performs a symbolic substitution across all the registers:
     *
     *  @code
     *   struct Substitution: BaseSemantics::RegisterStateGeneric::Visitor {
     *       SymbolicSemantics::SValuePtr from, to;
     *
     *       Substitution(const SymbolicSemantics::SValuePtr &from, const SymbolicSemantics::SValuePtr &to)
     *           : from(from), to(to) {}
     *
     *       BaseSemantics::SValuePtr operator()(const RegisterDescriptor &reg, const BaseSemantics::SValuePtr &val_) {
     *           SymbolicSemantics::SValuePtr val = SymbolicSemantics::SValue::promote(val_);
     *           return val->substitute(from, to);
     *       }
     *   };
     *
     *   SymbolicSemantics::SValuePtr original_esp = ...;
     *   SymbolicSemantics::SValuePtr fp = ...; // the frame pointer in terms of original_esp
     *   Substitution subst(original_esp, fp);
     *   RegisterStateGenericPtr regs = ...;
     *   std::cerr <<*regs; // register values before substitution
     *   regs->traverse(subst);
     *   std::cerr <<*regs; // all original_esp have been replaced by fp
     *  @endcode
     *
     * As with most ROSE and STL traversals, the Visitor is not allowed to modify the structure of the object over which it is
     * traversing.  In other words, it's permissible to change the values pointed to by the state, but it is not permissible to
     * perform any operation that might change the list of register parts by adding, removing, or combining parts.  This
     * includes calling readRegister() and writeRegister() except when the register being read or written is already exactly
     * stored in the state as indicated by is_exactly_stored().
     */
    virtual void traverse(Visitor&);


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Writer addresses
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Whether a register has writers.
     *
     *  Returns true if the specified register has writers. The "Any" version returns true if any bits of the register have
     *  writers, and the "All" version returns true if all bits of the register have writers (but not necessarily all the same
     *  writer).
     *
     * @{ */
    virtual bool hasWritersAny(const RegisterDescriptor&) const;
    virtual bool hasWritersAll(const RegisterDescriptor&) const;
    /** @} */

    /** Get writer information.
     *
     *  Returns all instruction addresses that have written to at least part of the specified register.  For instance, if
     *  instruction 0x1234 and 0x4321 wrote to AL and instruction 0x5678 wrote to AH then this method would return the set
     *  {0x1234, 0x4321, 0x5678} as the writers of AX. */
    virtual AddressSet getWritersUnion(const RegisterDescriptor&) const;

    /** Get writer information.
     *
     *  Returns the set of instruction addresses that have written to the entire specified register.  For instance, if
     *  instruction 0x1234 and 0x4321 wrote to AL and instructions 0x1234 and 0x5678 wrote to AH then this method will return
     *  the set {0x1234} as the writers of AX. */
    virtual AddressSet getWritersIntersection(const RegisterDescriptor&) const;

    /** Insert writer information.
     *
     *  Adds the specified instruction addresses as writers of the specified register.  Any previously existing writer
     *  addresses are not affected. Returns true if any addresses were inserted, false if they all already existed.  A single
     *  writer address can also be specified due to the AddressSet implicit constructor. */
    virtual bool insertWriters(const RegisterDescriptor&, const AddressSet &writerVas);

    /** Erase specified writers.
     *
     *  Removes the specified addresses from the set of writers for the register without affecting other addresses that might
     *  also be present. Returns true if none of the writer addresses existed, false if any were removed.  A single writer
     *  address can also be specified due to the AddressSet implicit constructor. */
    virtual void eraseWriters(const RegisterDescriptor&, const AddressSet &writerVas);

    /** Set writer information.
     *
     *  Changes the writer information to be exactly the specified address or set of addresses.  A single writer address can
     *  also be specified due to the AddressSet implicit constructor. */
    virtual void setWriters(const RegisterDescriptor&, const AddressSet &writers);

    /** Erase all writers.
     *
     *  If a register descriptor is provided then all writers are removed for that register only.  Otherwise all writers are
     *  removed for all registers.
     *
     * @{ */
    virtual void eraseWriters(const RegisterDescriptor&);
    virtual void eraseWriters();
    /** @} */


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Per-register bit Boolean properties
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:
    /** Whether a register has the specified property.
     *
     *  Returns true if the register has the specified property. The "Any" version returns true if any bits of the register
     *  have the property, while the "All" version returns true if all bits of the register have the property.
     *
     * @{ */
    virtual bool hasPropertyAny(const RegisterDescriptor&, RegisterProperty) const;
    virtual bool hasPropertyAll(const RegisterDescriptor&, RegisterProperty) const;
    /** @} */

    /** Get properties.
     *
     *  Returns the Boolean properties associated with the specified register.  The "Union" version returns the union of the
     *  properties across all bits of the register, while the "Intersection" version returns the set of properties that are
     *  defined for all bits of the register.
     *
     * @{ */
    virtual PropertySet getPropertiesUnion(const RegisterDescriptor&) const;
    virtual PropertySet getPropertiesIntersection(const RegisterDescriptor&) const;
    /** @} */

    /** Insert Boolean properties.
     *
     *  Inserts the specified properties for all bits of the specified register without affecting any other properties.
     *  Returns true if a property was inserted anywhere, false if all specified properties already existed everywhere in the
     *  specified register.  A single property can also be specified due to the RegisterProperties implicit constructor. */
    virtual bool insertProperties(const RegisterDescriptor&, const PropertySet&);

    /** Erase Boolean properties.
     *
     *  Removes the speciied properties from the specified register.  Returns true if any of the properties were erased, false
     *  if none of them already existed. A single property can also be specified due to the RegisterProperties implicit
     *  constructor. */
    virtual bool eraseProperties(const RegisterDescriptor&, const PropertySet&);

    /** Assign property set.
     *
     *  Assigns the specified property set (or single property) to the specified register. The register will then contain only
     *  those specified properties. */
    virtual void setProperties(const RegisterDescriptor&, const PropertySet&);

    /** Erase all Boolean properties.
     *
     *  Removes all properties from the specified register (or all registers).
     *
     * @{ */
    virtual void eraseProperties(const RegisterDescriptor&);
    virtual void eraseProperties();
    /** @} */


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Deprecated APIs
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
public:

    // Deprecated [Robb P. Matzke 2015-08-12]
    virtual void set_latest_writer(const RegisterDescriptor&, rose_addr_t writer_va) ROSE_DEPRECATED("use setWriters instead");
    virtual void clear_latest_writer(const RegisterDescriptor&) ROSE_DEPRECATED("use eraseWriters instead");
    virtual void clear_latest_writers() ROSE_DEPRECATED("use eraseWriters instead");
    virtual std::set<rose_addr_t> get_latest_writers(const RegisterDescriptor&) const
        ROSE_DEPRECATED("use getWritersUnion instead");

    virtual bool get_coalesceOnRead() ROSE_DEPRECATED("use coalesceOnRead instead") {
        return coalesceOnRead();
    }
    virtual bool set_coalesceOnRead(bool b=true) ROSE_DEPRECATED("use coalesceOnRead instead") {
        bool retval=coalesceOnRead();
        coalesceOnRead(b);
        return retval;
    }
    virtual bool clear_coalescOnRead() ROSE_DEPRECATED("use coalesceOnRead instead") {
        bool retval = coalesceOnRead();
        coalesceOnRead(false);
        return retval;
    }


    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
    //                                  Non-public APIs
    ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
protected:
    void deep_copy_values();
    static void get_nonoverlapping_parts(const Extent &overlap, const RegPair &rp, RiscOperators *ops,
                                         RegPairs *pairs/*out*/);
};

} // namespace
} // namespace
} // namespace
} // namespace

#endif

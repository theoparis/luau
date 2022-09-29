// This file is part of the Luau programming language and is licensed under MIT License; see LICENSE.txt for details
#pragma once

#include <memory>
#include <unordered_map>

#include "Luau/TypeVar.h"
#include "Luau/TypePack.h"

namespace Luau
{

using TypeOrPackId = const void*;

// Pending state for a TypeVar. Generated by a TxnLog and committed via
// TxnLog::commit.
struct PendingType
{
    // The pending TypeVar state.
    TypeVar pending;

    explicit PendingType(TypeVar state)
        : pending(std::move(state))
    {
    }
};

std::string toString(PendingType* pending);
std::string dump(PendingType* pending);

// Pending state for a TypePackVar. Generated by a TxnLog and committed via
// TxnLog::commit.
struct PendingTypePack
{
    // The pending TypePackVar state.
    TypePackVar pending;

    explicit PendingTypePack(TypePackVar state)
        : pending(std::move(state))
    {
    }
};

std::string toString(PendingTypePack* pending);
std::string dump(PendingTypePack* pending);

template<typename T>
T* getMutable(PendingType* pending)
{
    // We use getMutable here because this state is intended to be mutated freely.
    return getMutable<T>(&pending->pending);
}

template<typename T>
T* getMutable(PendingTypePack* pending)
{
    // We use getMutable here because this state is intended to be mutated freely.
    return getMutable<T>(&pending->pending);
}

// Log of what TypeIds we are rebinding, to be committed later.
struct TxnLog
{
    TxnLog()
        : typeVarChanges(nullptr)
        , typePackChanges(nullptr)
        , ownedSeen()
        , sharedSeen(&ownedSeen)
    {
    }

    explicit TxnLog(TxnLog* parent)
        : typeVarChanges(nullptr)
        , typePackChanges(nullptr)
        , parent(parent)
    {
        if (parent)
        {
            sharedSeen = parent->sharedSeen;
        }
        else
        {
            sharedSeen = &ownedSeen;
        }
    }

    explicit TxnLog(std::vector<std::pair<TypeOrPackId, TypeOrPackId>>* sharedSeen)
        : typeVarChanges(nullptr)
        , typePackChanges(nullptr)
        , sharedSeen(sharedSeen)
    {
    }

    TxnLog(const TxnLog&) = delete;
    TxnLog& operator=(const TxnLog&) = delete;

    TxnLog(TxnLog&&) = default;
    TxnLog& operator=(TxnLog&&) = default;

    // Gets an empty TxnLog pointer. This is useful for constructs that
    // take a TxnLog, like TypePackIterator - use the empty log if you
    // don't have a TxnLog to give it.
    static const TxnLog* empty();

    // Joins another TxnLog onto this one. You should use std::move to avoid
    // copying the rhs TxnLog.
    //
    // If both logs talk about the same type, pack, or table, the rhs takes
    // priority.
    void concat(TxnLog rhs);

    // Commits the TxnLog, rebinding all type pointers to their pending states.
    // Clears the TxnLog afterwards.
    void commit();

    // Clears the TxnLog without committing any pending changes.
    void clear();

    // Computes an inverse of this TxnLog at the current time.
    // This method should be called before commit is called in order to give an
    // accurate result. Committing the inverse of a TxnLog will undo the changes
    // made by commit, assuming the inverse log is accurate.
    TxnLog inverse();

    bool haveSeen(TypeId lhs, TypeId rhs) const;
    void pushSeen(TypeId lhs, TypeId rhs);
    void popSeen(TypeId lhs, TypeId rhs);

    bool haveSeen(TypePackId lhs, TypePackId rhs) const;
    void pushSeen(TypePackId lhs, TypePackId rhs);
    void popSeen(TypePackId lhs, TypePackId rhs);

    // Queues a type for modification. The original type will not change until commit
    // is called. Use pending to get the pending state.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingType* queue(TypeId ty);

    // Queues a type pack for modification. The original type pack will not change
    // until commit is called. Use pending to get the pending state.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingTypePack* queue(TypePackId tp);

    // Returns the pending state of a type, or nullptr if there isn't any. It is important
    // to note that this pending state is not transitive: the pending state may reference
    // non-pending types freely, so you may need to call pending multiple times to view the
    // entire pending state of a type graph.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingType* pending(TypeId ty) const;

    // Returns the pending state of a type pack, or nullptr if there isn't any. It is
    // important to note that this pending state is not transitive: the pending state may
    // reference non-pending types freely, so you may need to call pending multiple times
    // to view the entire pending state of a type graph.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingTypePack* pending(TypePackId tp) const;

    // Queues a replacement of a type with another type.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingType* replace(TypeId ty, TypeVar replacement);

    // Queues a replacement of a type pack with another type pack.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingTypePack* replace(TypePackId tp, TypePackVar replacement);

    // Queues a replacement of a table type with another table type that is bound
    // to a specific value.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingType* bindTable(TypeId ty, std::optional<TypeId> newBoundTo);

    // Queues a replacement of a type with a level with a duplicate of that type
    // with a new type level.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingType* changeLevel(TypeId ty, TypeLevel newLevel);

    // Queues a replacement of a type pack with a level with a duplicate of that
    // type pack with a new type level.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingTypePack* changeLevel(TypePackId tp, TypeLevel newLevel);

    // Queues the replacement of a type's scope with the provided scope.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingType* changeScope(TypeId ty, NotNull<Scope> scope);

    // Queues the replacement of a type pack's scope with the provided scope.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingTypePack* changeScope(TypePackId tp, NotNull<Scope> scope);

    // Queues a replacement of a table type with another table type with a new
    // indexer.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    PendingType* changeIndexer(TypeId ty, std::optional<TableIndexer> indexer);

    // Returns the type level of the pending state of the type, or the level of that
    // type, if no pending state exists. If the type doesn't have a notion of a level,
    // returns nullopt. If the pending state doesn't have a notion of a level, but the
    // original state does, returns nullopt.
    std::optional<TypeLevel> getLevel(TypeId ty) const;

    // Follows a type, accounting for pending type states. The returned type may have
    // pending state; you should use `pending` or `get` to find out.
    TypeId follow(TypeId ty) const;

    // Follows a type pack, accounting for pending type states. The returned type pack
    // may have pending state; you should use `pending` or `get` to find out.
    TypePackId follow(TypePackId tp) const;

    // Replaces a given type's state with a new variant. Returns the new pending state
    // of that type.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    template<typename T>
    PendingType* replace(TypeId ty, T replacement)
    {
        return replace(ty, TypeVar(replacement));
    }

    // Replaces a given type pack's state with a new variant. Returns the new
    // pending state of that type pack.
    //
    // The pointer returned lives until `commit` or `clear` is called.
    template<typename T>
    PendingTypePack* replace(TypePackId tp, T replacement)
    {
        return replace(tp, TypePackVar(replacement));
    }

    // Returns T if a given type or type pack is this variant, respecting the
    // log's pending state.
    //
    // Do not retain this pointer; it has the potential to be invalidated when
    // commit or clear is called.
    template<typename T, typename TID>
    T* getMutable(TID ty) const
    {
        auto* pendingTy = pending(ty);
        if (pendingTy)
            return Luau::getMutable<T>(pendingTy);

        return Luau::getMutable<T>(ty);
    }

    template<typename T, typename TID>
    const T* get(TID ty) const
    {
        return this->getMutable<T>(ty);
    }

    // Returns whether a given type or type pack is a given state, respecting the
    // log's pending state.
    //
    // This method will not assert if called on a BoundTypeVar or BoundTypePack.
    template<typename T, typename TID>
    bool is(TID ty) const
    {
        // We do not use getMutable here because this method can be called on
        // BoundTypeVars, which triggers an assertion.
        auto* pendingTy = pending(ty);
        if (pendingTy)
            return Luau::get_if<T>(&pendingTy->pending.ty) != nullptr;

        return Luau::get_if<T>(&ty->ty) != nullptr;
    }

    std::pair<std::vector<TypeId>, std::vector<TypePackId>> getChanges() const;

private:
    // unique_ptr is used to give us stable pointers across insertions into the
    // map. Otherwise, it would be really easy to accidentally invalidate the
    // pointers returned from queue/pending.
    DenseHashMap<TypeId, std::unique_ptr<PendingType>> typeVarChanges;
    DenseHashMap<TypePackId, std::unique_ptr<PendingTypePack>> typePackChanges;

    TxnLog* parent = nullptr;

    // Owned version of sharedSeen. This should not be accessed directly in
    // TxnLogs; use sharedSeen instead. This field exists because in the tree
    // of TxnLogs, the root must own its seen set. In all descendant TxnLogs,
    // this is an empty vector.
    std::vector<std::pair<TypeOrPackId, TypeOrPackId>> ownedSeen;

    bool haveSeen(TypeOrPackId lhs, TypeOrPackId rhs) const;
    void pushSeen(TypeOrPackId lhs, TypeOrPackId rhs);
    void popSeen(TypeOrPackId lhs, TypeOrPackId rhs);

public:
    // Used to avoid infinite recursion when types are cyclic.
    // Shared with all the descendent TxnLogs.
    std::vector<std::pair<TypeOrPackId, TypeOrPackId>>* sharedSeen;
};

} // namespace Luau

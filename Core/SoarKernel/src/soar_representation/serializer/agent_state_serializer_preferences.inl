void rebind_restored_preference_identities(agent* thisAgent, preference* pref, Symbol* goal)
{
    if (!thisAgent || !pref || !goal || !thisAgent->explanationBasedChunker)
    {
        return;
    }

    if (pref->inst_identities.id && pref->id && pref->id->is_sti())
    {
        thisAgent->explanationBasedChunker->force_add_inst_identity(pref->id, pref->inst_identities.id);
    }
    if (pref->inst_identities.value && pref->value && pref->value->is_sti())
    {
        thisAgent->explanationBasedChunker->force_add_inst_identity(pref->value, pref->inst_identities.value);
    }
    if (pref->inst_identities.referent && pref->referent && pref->referent->is_sti())
    {
        thisAgent->explanationBasedChunker->force_add_inst_identity(pref->referent, pref->inst_identities.referent);
    }

    if (pref->inst_identities.id)
    {
        if (pref->identities.id)
        {
            thisAgent->explanationBasedChunker->force_id_to_identity_mapping(pref->inst_identities.id,
                                                                             pref->identities.id);
        }
        Identity* identity = thisAgent->explanationBasedChunker->get_or_add_identity(pref->inst_identities.id,
                                                                                      pref->identities.id,
                                                                                      goal);
        if (identity)
        {
            set_pref_identity(thisAgent,
                              pref,
                              ID_ELEMENT,
                              identity);
        }
    }
    if (pref->inst_identities.attr)
    {
        if (pref->identities.attr)
        {
            thisAgent->explanationBasedChunker->force_id_to_identity_mapping(pref->inst_identities.attr,
                                                                             pref->identities.attr);
        }
        Identity* identity = thisAgent->explanationBasedChunker->get_or_add_identity(pref->inst_identities.attr,
                                                                                      pref->identities.attr,
                                                                                      goal);
        if (identity)
        {
            set_pref_identity(thisAgent,
                              pref,
                              ATTR_ELEMENT,
                              identity);
        }
    }
    if (pref->inst_identities.value)
    {
        if (pref->identities.value)
        {
            thisAgent->explanationBasedChunker->force_id_to_identity_mapping(pref->inst_identities.value,
                                                                             pref->identities.value);
        }
        Identity* identity = thisAgent->explanationBasedChunker->get_or_add_identity(pref->inst_identities.value,
                                                                                      pref->identities.value,
                                                                                      goal);
        if (identity)
        {
            set_pref_identity(thisAgent,
                              pref,
                              VALUE_ELEMENT,
                              identity);
        }
    }
    if (pref->inst_identities.referent)
    {
        if (pref->identities.referent)
        {
            thisAgent->explanationBasedChunker->force_id_to_identity_mapping(pref->inst_identities.referent,
                                                                             pref->identities.referent);
        }
        Identity* identity = thisAgent->explanationBasedChunker->get_or_add_identity(pref->inst_identities.referent,
                                                                                      pref->identities.referent,
                                                                                      goal);
        if (identity)
        {
            set_pref_identity(thisAgent,
                              pref,
                              REFERENT_ELEMENT,
                              identity);
        }
    }
}

void add_restored_preference_refs(agent* thisAgent, preference* pref)
{
    if (!pref) return;

    if (pref->id)
    {
        thisAgent->symbolManager->symbol_add_ref(pref->id);
    }
    if (pref->attr)
    {
        thisAgent->symbolManager->symbol_add_ref(pref->attr);
    }
    if (pref->value)
    {
        thisAgent->symbolManager->symbol_add_ref(pref->value);
    }
    if (preference_is_binary(pref->type) && pref->referent)
    {
        thisAgent->symbolManager->symbol_add_ref(pref->referent);
    }
}

void remove_restored_preference_refs(agent* thisAgent, preference* pref)
{
    if (!pref) return;

    if (pref->id)
    {
        thisAgent->symbolManager->symbol_remove_ref(&pref->id);
    }
    if (pref->attr)
    {
        thisAgent->symbolManager->symbol_remove_ref(&pref->attr);
    }
    if (pref->value)
    {
        thisAgent->symbolManager->symbol_remove_ref(&pref->value);
    }
    if (preference_is_binary(pref->type) && pref->referent)
    {
        thisAgent->symbolManager->symbol_remove_ref(&pref->referent);
    }
}

void deallocate_restored_preferences(agent* thisAgent, RestoredPreferenceMap& preference_map)
{
    std::unordered_set<preference*> processed;
    for (auto& entry : preference_map)
    {
        preference* pref = entry.second;
        if (!pref || !processed.insert(pref).second)
        {
            continue;
        }

        const bool unattached = (!pref->slot && !pref->in_tm && !pref->inst);
        if (unattached)
        {
            deallocate_preference(thisAgent, pref, true);
        }
    }

    preference_map.clear();
}

void deallocate_synthetic_restored_preferences(agent* thisAgent,
                                               const SyntheticPreferenceLifecycle& synthetic_lifecycle)
{
    /* Synthetic preferences are explicitly tracked at creation time to avoid
       heuristic teardown based on mutable fields (inst/reference_count).
       - restore_owned: temporary refs owned by restore pipeline and released here
       - init_owned: refs intentionally handed to init-soar teardown lifecycle */
    for (preference* pref : synthetic_lifecycle.restore_owned)
    {
        if (pref && (synthetic_lifecycle.init_owned.find(pref) == synthetic_lifecycle.init_owned.end()))
        {
            preference_remove_ref(thisAgent, pref);
        }
    }
}

void restore_preference_identity_bindings(agent* thisAgent,
                                         preference* pref,
                                         const soar::kernel::PreferenceEntry& pref_entry,
                                         Symbol* goal)
{
    if (!pref || !goal || !goal->is_sti())
    {
        return;
    }

    auto restore_identity = [&](uint64_t identity_id, Identity*& destination)
    {
        if (!identity_id)
        {
            return;
        }

        Identity* identity = get_or_create_restored_identity_by_id(thisAgent, goal, identity_id);
        if (identity)
        {
            destination = identity;
        }
    };

    if (pref_entry.identity_id())
    {
        restore_identity(pref_entry.identity_id(), pref->identities.id);
    }
    if (pref_entry.identity_attr())
    {
        restore_identity(pref_entry.identity_attr(), pref->identities.attr);
    }
    if (pref_entry.identity_value())
    {
        restore_identity(pref_entry.identity_value(), pref->identities.value);
    }
    if (pref_entry.identity_referent())
    {
        restore_identity(pref_entry.identity_referent(), pref->identities.referent);
    }
}

instantiation* create_restored_preference_instantiation(agent* thisAgent, preference* pref, goal_stack_level match_goal_level)
{
    if (!pref)
    {
        return NIL;
    }

    instantiation* inst = NIL;
    init_instantiation(thisAgent, inst, thisAgent->symbolManager->soarSymbols.architecture_inst_symbol);

    Symbol* goal = NIL;
    if (match_goal_level > 0)
    {
        goal = goal_for_level(thisAgent, match_goal_level);
    }
    if (!goal && pref->id && pref->id->is_sti())
    {
        goal = goal_for_level(thisAgent, pref->id->id->level);
    }
    if (!goal)
    {
        goal = thisAgent->top_goal;
    }

    const bool saved_o_supported = pref->o_supported;
    inst->match_goal = goal;
    inst->match_goal_level = goal ? goal->id->level : match_goal_level;
    add_pref_to_inst(thisAgent, pref, inst);

    if (inst->match_goal)
    {
        insert_at_head_of_dll(inst->match_goal->id->preferences_from_goal, pref, all_of_goal_next, all_of_goal_prev);
        pref->on_goal_list = true;
    }

    pref->o_supported = saved_o_supported;

    return inst;
}

Symbol* goal_for_level(agent* thisAgent, goal_stack_level level)
{
    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (goal->id->level == level)
        {
            return goal;
        }
    }

    return NIL;
}

void register_restored_preference(agent* thisAgent, preference* pref, goal_stack_level match_goal_level)
{
    if (!pref)
    {
        return;
    }

    if (!pref->inst)
    {
        create_restored_preference_instantiation(thisAgent, pref, match_goal_level);
    }

    if (pref->in_tm)
    {
        add_preference_to_tm(thisAgent, pref);
    }
    else
    {
        preference_add_ref(pref);
    }
}

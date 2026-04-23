preference* get_or_create_restored_preference(agent* thisAgent,
                                              const soar::kernel::PreferenceEntry& pref_entry,
                                              Symbol* id,
                                              Symbol* attr,
                                              Symbol* value,
                                              const SymbolReverseMap& symbol_map,
                                              RestoredPreferenceMap& preference_map,
                                              RestoredPreferenceCloneMap& preference_clone_map)
{
    int pref_type_int = static_cast<int>(pref_entry.type());
    if (!preference_type_is_valid(pref_type_int))
    {
        return NIL;
    }

    ::PreferenceType pref_type = static_cast< ::PreferenceType >(pref_type_int);
    Symbol* referent = symbol_by_id(symbol_map, pref_entry.referent());
    goal_stack_level match_goal_level = static_cast<goal_stack_level>(pref_entry.match_goal_level());
    if (match_goal_level <= 0 && id && id->is_sti())
    {
        match_goal_level = id->id->level;
    }

    RestoredPreferenceKey key = make_restored_preference_key(pref_type,
                                                             pref_entry.o_supported(),
                                                             pref_entry.in_tm(),
                                                             match_goal_level,
                                                             id,
                                                             attr,
                                                             value,
                                                             referent);
    auto existing = preference_map.find(key);
    if (existing != preference_map.end())
    {
        preference* pref = existing->second;
        if (!pref->inst_identities.id && pref_entry.inst_identity_id())
        {
            pref->inst_identities.id = pref_entry.inst_identity_id();
        }
        if (!pref->inst_identities.attr && pref_entry.inst_identity_attr())
        {
            pref->inst_identities.attr = pref_entry.inst_identity_attr();
        }
        if (!pref->inst_identities.value && pref_entry.inst_identity_value())
        {
            pref->inst_identities.value = pref_entry.inst_identity_value();
        }
        if (!pref->inst_identities.referent && pref_entry.inst_identity_referent())
        {
            pref->inst_identities.referent = pref_entry.inst_identity_referent();
        }

        if (pref->inst && pref->inst->match_goal)
        {
            rebind_restored_preference_identities(thisAgent, pref, pref->inst->match_goal);
            restore_preference_identity_bindings(thisAgent, pref, pref_entry, pref->inst->match_goal);
        }
        else if (id && id->is_sti())
        {
            restore_preference_identity_bindings(thisAgent, pref, pref_entry, id);
        }

        return existing->second;
    }

    RestoredPreferenceCloneKey clone_key = make_restored_preference_clone_key(pref_type,
                                                                              pref_entry.o_supported(),
                                                                              pref_entry.in_tm(),
                                                                              id,
                                                                              attr,
                                                                              value,
                                                                              referent);
    auto existing_clone = preference_clone_map.find(clone_key);
    if (existing_clone != preference_clone_map.end())
    {
        preference* pref = existing_clone->second;
        if (pref)
        {
            if (!pref->inst_identities.id && pref_entry.inst_identity_id())
            {
                pref->inst_identities.id = pref_entry.inst_identity_id();
            }
            if (!pref->inst_identities.attr && pref_entry.inst_identity_attr())
            {
                pref->inst_identities.attr = pref_entry.inst_identity_attr();
            }
            if (!pref->inst_identities.value && pref_entry.inst_identity_value())
            {
                pref->inst_identities.value = pref_entry.inst_identity_value();
            }
            if (!pref->inst_identities.referent && pref_entry.inst_identity_referent())
            {
                pref->inst_identities.referent = pref_entry.inst_identity_referent();
            }

            if (pref->inst && pref->inst->match_goal)
            {
                rebind_restored_preference_identities(thisAgent, pref, pref->inst->match_goal);
                restore_preference_identity_bindings(thisAgent, pref, pref_entry, pref->inst->match_goal);
            }
            else if (id && id->is_sti())
            {
                restore_preference_identity_bindings(thisAgent, pref, pref_entry, id);
            }

            preference_map.emplace(key, pref);
            return pref;
        }
    }

    preference* pref = make_preference(thisAgent, pref_type, id, attr, value, referent);
    pref->inst = NIL;
    pref->o_supported = pref_entry.o_supported();
    pref->in_tm = pref_entry.in_tm();
    pref->inst_identities.id = pref_entry.inst_identity_id();
    pref->inst_identities.attr = pref_entry.inst_identity_attr();
    pref->inst_identities.value = pref_entry.inst_identity_value();
    pref->inst_identities.referent = pref_entry.inst_identity_referent();
    add_restored_preference_refs(thisAgent, pref);
    register_restored_preference(thisAgent, pref, match_goal_level);
    if (pref->inst && pref->inst->match_goal)
    {
        rebind_restored_preference_identities(thisAgent, pref, pref->inst->match_goal);
        restore_preference_identity_bindings(thisAgent, pref, pref_entry, pref->inst->match_goal);
    }
    else if (id && id->is_sti())
    {
        restore_preference_identity_bindings(thisAgent, pref, pref_entry, id);
    }
    link_restored_preference_clone(pref, clone_key, preference_clone_map);
    preference_map.emplace(key, pref);
    return pref;
}

void reassign_restored_preference_instantiation(agent* thisAgent,
                                                preference* pref,
                                                instantiation* inst,
                                                SavedInstantiationSet* cleaned_previous_insts);

preference* get_or_create_restored_preference(agent* thisAgent,
                                              const soar::kernel::WmeEntry& wme_entry,
                                              Symbol* id,
                                              Symbol* attr,
                                              Symbol* value,
                                              const SymbolReverseMap& symbol_map,
                                              RestoredPreferenceMap& preference_map,
                                              RestoredPreferenceCloneMap& preference_clone_map)
{
    if (!wme_entry.has_preference())
    {
        return NIL;
    }

    return get_or_create_restored_preference(thisAgent,
                                             wme_entry.preference(),
                                             id,
                                             attr,
                                             value,
                                             symbol_map,
                                             preference_map,
                                             preference_clone_map);
}

preference* ensure_goal_impasse_item_preference(agent* thisAgent,
                                                RestoredWmeOwner owner,
                                                Symbol* id,
                                                Symbol* attr,
                                                Symbol* value,
                                                preference* pref,
                                                SyntheticPreferenceLifecycle* synthetic_lifecycle)
{
    if (pref || owner != RestoredWmeOwner::Impasse || !id || !id->is_sti() || !id->id->isa_goal)
    {
        return pref;
    }

    if ((attr != thisAgent->symbolManager->soarSymbols.item_symbol) &&
        (attr != thisAgent->symbolManager->soarSymbols.non_numeric_symbol))
    {
        return pref;
    }

    /* Goal impasse item WMEs are removed with preference_remove_ref() during init.
       Ensure restored WMEs have an owned synthetic preference to match that lifecycle. */
    preference* synthetic_pref = make_preference(thisAgent,
                                                 ::ACCEPTABLE_PREFERENCE_TYPE,
                                                 id,
                                                 attr,
                                                 value,
                                                 NIL);
     /* Synthetic preferences are not created via normal instantiation paths,
         so take explicit symbol refs that deallocate_preference() will release. */
     thisAgent->symbolManager->symbol_add_ref(synthetic_pref->id);
     thisAgent->symbolManager->symbol_add_ref(synthetic_pref->attr);
     thisAgent->symbolManager->symbol_add_ref(synthetic_pref->value);
    synthetic_pref->inst = NIL;
    synthetic_pref->o_supported = false;
    synthetic_pref->in_tm = false;
    preference_add_ref(synthetic_pref);
    if (synthetic_lifecycle)
    {
        synthetic_lifecycle->track_init_owned(synthetic_pref);
    }
    return synthetic_pref;
}

void restore_wma_decay_state(agent* thisAgent,
                             wme* w,
                             const soar::kernel::WmeEntry& wme_entry)
{
    if (!w || !wme_entry.has_wma_decay())
    {
        return;
    }

    wma_decay_element* decay_el = NIL;
    thisAgent->memoryManager->allocate_with_pool(MP_wma_decay_element, &decay_el);
    decay_el->this_wme = w;
    decay_el->just_removed = wme_entry.wma_just_removed();
    decay_el->just_created = wme_entry.wma_just_created();
    decay_el->num_references = static_cast<wma_reference>(wme_entry.wma_num_references());
    decay_el->forget_cycle = static_cast<wma_d_cycle>(wme_entry.wma_forget_cycle());
    decay_el->touches.history_references = static_cast<wma_reference>(wme_entry.wma_history_references());
    decay_el->touches.total_references = static_cast<wma_reference>(wme_entry.wma_total_references());
    decay_el->touches.first_reference = static_cast<wma_d_cycle>(wme_entry.wma_first_reference());
    decay_el->touches.next_p = static_cast<unsigned int>(wme_entry.wma_next_p()) % WMA_DECAY_HISTORY;
    decay_el->touches.history_ct = std::min(static_cast<unsigned int>(wme_entry.wma_history_ct()), static_cast<unsigned int>(WMA_DECAY_HISTORY));

    const int cycle_count = std::min(wme_entry.wma_access_cycles_size(), WMA_DECAY_HISTORY);
    const int reference_count = std::min(wme_entry.wma_access_references_size(), WMA_DECAY_HISTORY);
    const int history_count = std::min(cycle_count, reference_count);
    for (int history_index = 0; history_index < WMA_DECAY_HISTORY; ++history_index)
    {
        if (history_index < history_count)
        {
            decay_el->touches.access_history[history_index].d_cycle = static_cast<wma_d_cycle>(wme_entry.wma_access_cycles(history_index));
            decay_el->touches.access_history[history_index].num_references = static_cast<wma_reference>(wme_entry.wma_access_references(history_index));
        }
        else
        {
            decay_el->touches.access_history[history_index].d_cycle = 0;
            decay_el->touches.access_history[history_index].num_references = 0;
        }
    }

    w->wma_decay_el = decay_el;

    if (decay_el->forget_cycle > WMA_FORGOTTEN_CYCLE)
    {
        wma_forget_p_queue::iterator pq_it = thisAgent->WM->wma_forget_pq->find(decay_el->forget_cycle);
        if (pq_it == thisAgent->WM->wma_forget_pq->end())
        {
            wma_decay_set* new_bucket = new wma_decay_set();
            thisAgent->WM->wma_forget_pq->insert(std::make_pair(decay_el->forget_cycle, new_bucket));
            pq_it = thisAgent->WM->wma_forget_pq->find(decay_el->forget_cycle);
        }
        pq_it->second->insert(decay_el);
    }
}

void collect_restored_wma_o_set_members(wme* source,
                                        wme_set* o_set,
                                        std::unordered_set<wme*>& visited_wmes,
                                        std::unordered_set<preference*>& visited_prefs)
{
    if (!source || !o_set)
    {
        return;
    }

    if (!visited_wmes.insert(source).second)
    {
        return;
    }

    if (wma_should_have_decay_element(source))
    {
        o_set->insert(source);
        return;
    }

    if (!source->preference)
    {
        if (source->reference_count != 0)
        {
            o_set->insert(source);
        }
        return;
    }

    if ((source->preference->reference_count == 0) || !source->preference->inst)
    {
        return;
    }

    if (!visited_prefs.insert(source->preference).second)
    {
        return;
    }

    for (condition* cond = source->preference->inst->top_of_instantiated_conditions; cond; cond = cond->next)
    {
        if ((cond->type == POSITIVE_CONDITION) && cond->bt.wme_)
        {
            collect_restored_wma_o_set_members(cond->bt.wme_, o_set, visited_wmes, visited_prefs);
        }
    }
}

void rebuild_restored_wma_o_sets(agent* thisAgent,
                                 const RestoredPreferenceMap& preference_map)
{
    if (!thisAgent || !wma_enabled(thisAgent))
    {
        return;
    }

    std::unordered_set<preference*> rebuilt_prefs;
    for (const auto& restored_pref_entry : preference_map)
    {
        preference* pref = restored_pref_entry.second;
        if (!pref || !rebuilt_prefs.insert(pref).second)
        {
            continue;
        }

        if (pref->o_supported || !pref->inst || (pref->reference_count == 0) || pref->wma_o_set)
        {
            continue;
        }

        wme* source_wme = NIL;
        for (wme* w = thisAgent->all_wmes_in_rete; w != NIL; w = w->rete_next)
        {
            if (w->preference == pref)
            {
                source_wme = w;
                break;
            }
        }

        if (!source_wme)
        {
            continue;
        }

        wme_set* rebuilt_o_set = NIL;
        thisAgent->memoryManager->allocate_with_pool(MP_wma_wme_oset, &rebuilt_o_set);
        rebuilt_o_set = new(rebuilt_o_set) wme_set();

        std::unordered_set<wme*> visited_wmes;
        std::unordered_set<preference*> visited_prefs;
        collect_restored_wma_o_set_members(source_wme, rebuilt_o_set, visited_wmes, visited_prefs);

        if (rebuilt_o_set->empty())
        {
            rebuilt_o_set->~wme_set();
            thisAgent->memoryManager->free_with_pool(MP_wma_wme_oset, rebuilt_o_set);
            continue;
        }

        pref->wma_o_set = rebuilt_o_set;
        for (wme* member : *rebuilt_o_set)
        {
            wme_add_ref(member, true);
        }
    }
}

void purge_runtime_shell_wmes(agent* thisAgent)
{
    if (!thisAgent)
    {
        return;
    }

    if (thisAgent->existing_output_links != NIL)
    {
        output_link* current = thisAgent->existing_output_links;
        while (current)
        {
            output_link* next = current->next;

            if (current->ids_in_tc)
            {
                remove_output_link_tc_info(thisAgent, current);
            }

            if (current->link_wme)
            {
                current->link_wme->output_link = NIL;
                wme_remove_ref(thisAgent, current->link_wme);
            }

            thisAgent->memoryManager->free_with_pool(MP_output_link, current);
            current = next;
        }

        thisAgent->existing_output_links = NIL;
        thisAgent->output_link_for_tc = NIL;
        thisAgent->output_link_tc_num = 0;
        thisAgent->collected_io_wmes = NIL;
        thisAgent->output_link_changed = false;
    }

    if (thisAgent->all_wmes_in_rete == NIL)
    {
        return;
    }

    std::vector<wme*> live_wmes;
    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        live_wmes.push_back(current);
    }

    for (wme* current : live_wmes)
    {
        detach_restored_wme_from_owner(thisAgent, current);
        remove_wme_from_wm(thisAgent, current);
    }

    if (thisAgent->wmes_to_add || thisAgent->wmes_to_remove)
    {
        do_buffered_wm_and_ownership_changes(thisAgent);
    }
}

void create_wmes_from_entries(agent* thisAgent,
                              const google::protobuf::RepeatedPtrField<soar::kernel::WmeEntry>& entries,
                              const SymbolReverseMap& symbol_map,
                              RestoredPreferenceMap& preference_map,
                              RestoredPreferenceCloneMap& preference_clone_map,
                              SyntheticPreferenceLifecycle* synthetic_lifecycle,
                              bool add_to_rete = true)
{
    const bool debug_wme_ref_track = (std::getenv("SOAR_DEBUG_WME_REF_TRACK") != nullptr);
    const bool debug_wme_provenance = (std::getenv("SOAR_DEBUG_WME_PROVENANCE") != nullptr);
    auto is_tracked_identifier = [](Symbol* sym)
    {
        if (!sym || !sym->is_sti())
        {
            return false;
        }
        const char letter = sym->id->name_letter;
        const uint64_t number = sym->id->name_number;
        return ((letter == 'I') && ((number == 2) || (number == 3))) ||
               ((letter == 'O') && (number == 1)) ||
               ((letter == 'S') && ((number == 1) || (number == 2)));
    };
    auto is_provenance_identifier = [](Symbol* sym)
    {
        if (!sym || !sym->is_sti())
        {
            return false;
        }
        const char letter = sym->id->name_letter;
        const uint64_t number = sym->id->name_number;
        return ((letter == 'J') && (number == 2)) ||
               ((letter == 'O') && ((number == 1) || (number == 2) || (number == 4) || (number == 5))) ||
               ((letter == 'S') && (number == 1));
    };
    auto owner_name = [](RestoredWmeOwner owner)
    {
        switch (owner)
        {
            case RestoredWmeOwner::Slot: return "slot";
            case RestoredWmeOwner::AcceptablePreference: return "acceptable";
            case RestoredWmeOwner::Impasse: return "impasse";
            case RestoredWmeOwner::Input: return "input";
            default: return "unknown";
        }
    };

    purge_runtime_shell_wmes(thisAgent);

    uint64_t max_timetag = thisAgent->current_wme_timetag;
    for (const auto& wme_entry : entries)
    {
        Symbol* id = symbol_by_id(symbol_map, wme_entry.id_symbol());
        Symbol* attr = symbol_by_id(symbol_map, wme_entry.attr_symbol());
        Symbol* value = symbol_by_id(symbol_map, wme_entry.value_symbol());
        if (!id || !attr || !value) continue;

        const bool tracked_id = is_tracked_identifier(id);
        const bool tracked_value = is_tracked_identifier(value);
        const bool trace_this_wme = debug_wme_ref_track && (tracked_id || tracked_value);
        const uint64_t id_ref_before_make = tracked_id ? id->reference_count : 0;
        const uint64_t value_ref_before_make = tracked_value ? value->reference_count : 0;

        wme* w = make_wme(thisAgent, id, attr, value, wme_entry.acceptable());
        if (trace_this_wme)
        {
            std::fprintf(stderr,
                         "restore_wme_ref: stage=post-make timetag=%llu owner_proto=%d id=%c%llu id_ref=%llu(%+lld) value=%c%llu value_ref=%llu(%+lld)\n",
                         static_cast<unsigned long long>(wme_entry.timetag()),
                         static_cast<int>(wme_entry.owner_type()),
                         tracked_id ? id->id->name_letter : '?',
                         static_cast<unsigned long long>(tracked_id ? id->id->name_number : 0),
                         static_cast<unsigned long long>(tracked_id ? id->reference_count : 0),
                         tracked_id ? (static_cast<long long>(id->reference_count) - static_cast<long long>(id_ref_before_make)) : 0,
                         tracked_value ? value->id->name_letter : '?',
                         static_cast<unsigned long long>(tracked_value ? value->id->name_number : 0),
                         static_cast<unsigned long long>(tracked_value ? value->reference_count : 0),
                         tracked_value ? (static_cast<long long>(value->reference_count) - static_cast<long long>(value_ref_before_make)) : 0);
        }
        w->timetag = wme_entry.timetag();
        if (id->is_sti())
        {
            if (Identity* identity = find_goal_identity_by_id(thisAgent, id, wme_entry.local_singleton_id_identity()))
            {
                identity->add_ref();
                w->local_singleton_id_identity_set = identity;
            }
            if (Identity* identity = find_goal_identity_by_id(thisAgent, id, wme_entry.local_singleton_value_identity()))
            {
                identity->add_ref();
                w->local_singleton_value_identity_set = identity;
            }
        }
        preference* pref = get_or_create_restored_preference(thisAgent, wme_entry, id, attr, value, symbol_map, preference_map, preference_clone_map);
        RestoredWmeOwner owner = owner_from_proto_type(wme_entry.owner_type(), thisAgent, wme_entry, id, attr, pref);
        pref = ensure_goal_impasse_item_preference(thisAgent, owner, id, attr, value, pref, synthetic_lifecycle);
        if (debug_wme_provenance && (is_provenance_identifier(id) || is_provenance_identifier(value)))
        {
            const char* pref_prod_name = "<none>";
            if (pref && pref->inst && pref->inst->prod && pref->inst->prod->name)
            {
                const char* rendered = pref->inst->prod->name->to_string(true);
                if (rendered)
                {
                    pref_prod_name = rendered;
                }
            }
            std::fprintf(stderr,
                         "restore_wme_provenance: tt=%llu id=%s attr=%s value=%s owner=%s entry_create=%llu entry_support=%llu pref_inst=%p pref_prod=%s\n",
                         static_cast<unsigned long long>(wme_entry.timetag()),
                         id ? id->to_string(true, false, NIL, 0) : "<nil>",
                         attr ? attr->to_string(true, false, NIL, 0) : "<nil>",
                         value ? value->to_string(true, false, NIL, 0) : "<nil>",
                         owner_name(owner),
                         static_cast<unsigned long long>(wme_entry.creating_instantiation_id()),
                         static_cast<unsigned long long>(wme_entry.supporting_instantiation_id()),
                         static_cast<void*>(pref ? pref->inst : NIL),
                         pref_prod_name);
        }
        w->preference = pref;
        restore_wma_decay_state(thisAgent, w, wme_entry);

        const uint64_t id_ref_before_attach = tracked_id ? id->reference_count : 0;
        const uint64_t value_ref_before_attach = tracked_value ? value->reference_count : 0;
        attach_restored_wme_to_owner(thisAgent, w, owner);
        if (trace_this_wme)
        {
            std::fprintf(stderr,
                         "restore_wme_ref: stage=post-attach timetag=%llu owner=%s id=%c%llu id_ref=%llu(%+lld) value=%c%llu value_ref=%llu(%+lld)\n",
                         static_cast<unsigned long long>(wme_entry.timetag()),
                         owner_name(owner),
                         tracked_id ? id->id->name_letter : '?',
                         static_cast<unsigned long long>(tracked_id ? id->id->name_number : 0),
                         static_cast<unsigned long long>(tracked_id ? id->reference_count : 0),
                         tracked_id ? (static_cast<long long>(id->reference_count) - static_cast<long long>(id_ref_before_attach)) : 0,
                         tracked_value ? value->id->name_letter : '?',
                         static_cast<unsigned long long>(tracked_value ? value->id->name_number : 0),
                         static_cast<unsigned long long>(tracked_value ? value->reference_count : 0),
                         tracked_value ? (static_cast<long long>(value->reference_count) - static_cast<long long>(value_ref_before_attach)) : 0);
        }

        if (pref && owner == RestoredWmeOwner::Slot)
        {
            slot* s = find_slot(id, attr);
            if (s && s->isa_context_slot)
            {
                preference_add_ref(pref);
            }
        }

        if (w->timetag >= max_timetag)
        {
            max_timetag = w->timetag + 1;
        }
        if (add_to_rete)
        {
            const uint64_t id_ref_before_add_wm = tracked_id ? id->reference_count : 0;
            const uint64_t value_ref_before_add_wm = tracked_value ? value->reference_count : 0;
            add_wme_to_wm(thisAgent, w);
            if (trace_this_wme)
            {
                std::fprintf(stderr,
                             "restore_wme_ref: stage=post-add-wm timetag=%llu id=%c%llu id_ref=%llu(%+lld) value=%c%llu value_ref=%llu(%+lld)\n",
                             static_cast<unsigned long long>(wme_entry.timetag()),
                             tracked_id ? id->id->name_letter : '?',
                             static_cast<unsigned long long>(tracked_id ? id->id->name_number : 0),
                             static_cast<unsigned long long>(tracked_id ? id->reference_count : 0),
                             tracked_id ? (static_cast<long long>(id->reference_count) - static_cast<long long>(id_ref_before_add_wm)) : 0,
                             tracked_value ? value->id->name_letter : '?',
                             static_cast<unsigned long long>(tracked_value ? value->id->name_number : 0),
                             static_cast<unsigned long long>(tracked_value ? value->reference_count : 0),
                             tracked_value ? (static_cast<long long>(value->reference_count) - static_cast<long long>(value_ref_before_add_wm)) : 0);
            }
        }
        else
        {
            insert_at_head_of_dll(thisAgent->all_wmes_in_rete, w, rete_next, rete_prev);
            thisAgent->num_wmes_in_rete++;
            w->right_mems = NIL;
            w->tokens = NIL;
            wme_add_ref(w, true);
        }
    }
    thisAgent->current_wme_timetag = max_timetag;
}

void rebind_restored_wme_preferences_by_provenance(agent* thisAgent,
                                                   const google::protobuf::RepeatedPtrField<soar::kernel::WmeEntry>& entries,
                                                   const std::unordered_map<uint64_t, instantiation*>& restored_instantiation_id_map)
{
    if (!thisAgent || restored_instantiation_id_map.empty())
    {
        return;
    }

    const bool debug_wme_provenance = (std::getenv("SOAR_DEBUG_WME_PROVENANCE") != nullptr);
    Symbol* arch_inst_symbol = thisAgent->symbolManager->soarSymbols.architecture_inst_symbol;
    uint64_t scanned_candidates = 0;
    uint64_t rebound_preferences = 0;
    uint64_t missing_instantiation_targets = 0;

    std::unordered_map<uint64_t, wme*> wme_by_timetag;
    for (wme* restored_wme = thisAgent->all_wmes_in_rete; restored_wme != NIL; restored_wme = restored_wme->rete_next)
    {
        wme_by_timetag[restored_wme->timetag] = restored_wme;
    }

    for (const auto& wme_entry : entries)
    {
        if (!wme_entry.has_preference() || !wme_entry.supporting_instantiation_id())
        {
            continue;
        }

        ++scanned_candidates;
        auto target_it = restored_instantiation_id_map.find(wme_entry.supporting_instantiation_id());
        auto wme_it = wme_by_timetag.find(wme_entry.timetag());
        if (wme_it == wme_by_timetag.end() || !wme_it->second || !wme_it->second->preference)
        {
            continue;
        }

        preference* pref = wme_it->second->preference;
        const bool already_non_synthetic =
            pref->inst && pref->inst->prod_name && (pref->inst->prod_name != arch_inst_symbol);
        if (already_non_synthetic)
        {
            continue;
        }

        if (target_it == restored_instantiation_id_map.end() || !target_it->second)
        {
            ++missing_instantiation_targets;
            if (debug_wme_provenance)
            {
                const char* current_prod = "<none>";
                if (pref->inst && pref->inst->prod_name && pref->inst->prod_name->is_string())
                {
                    current_prod = pref->inst->prod_name->sc->name;
                }

                std::fprintf(stderr,
                             "restore_wme_provenance_rebind_miss: tt=%llu support_inst=%llu pref_inst=%p pref_prod=%s\n",
                             static_cast<unsigned long long>(wme_entry.timetag()),
                             static_cast<unsigned long long>(wme_entry.supporting_instantiation_id()),
                             static_cast<void*>(pref->inst),
                             current_prod);
            }
            continue;
        }

        if (pref->inst == target_it->second)
        {
            continue;
        }

        reassign_restored_preference_instantiation(thisAgent, pref, target_it->second, nullptr);
        ++rebound_preferences;

        if (debug_wme_provenance)
        {
            const char* prod_name = "<none>";
            if (pref->inst && pref->inst->prod_name && pref->inst->prod_name->is_string())
            {
                prod_name = pref->inst->prod_name->sc->name;
            }

            std::fprintf(stderr,
                         "restore_wme_provenance_rebind: tt=%llu support_inst=%llu pref_inst=%p pref_prod=%s\n",
                         static_cast<unsigned long long>(wme_entry.timetag()),
                         static_cast<unsigned long long>(wme_entry.supporting_instantiation_id()),
                         static_cast<void*>(pref->inst),
                         prod_name);
        }
    }

    if (debug_wme_provenance)
    {
        std::fprintf(stderr,
                     "restore_wme_provenance_rebind_summary: map_size=%llu scanned=%llu rebound=%llu missing_targets=%llu\n",
                     static_cast<unsigned long long>(restored_instantiation_id_map.size()),
                     static_cast<unsigned long long>(scanned_candidates),
                     static_cast<unsigned long long>(rebound_preferences),
                     static_cast<unsigned long long>(missing_instantiation_targets));
    }
}

void apply_buffered_remove_wmes(agent* thisAgent,
                                const google::protobuf::RepeatedPtrField<soar::kernel::WmeEntry>& entries,
                                const SymbolReverseMap& symbol_map)
{
    for (const auto& wme_entry : entries)
    {
        Symbol* id = symbol_by_id(symbol_map, wme_entry.id_symbol());
        Symbol* attr = symbol_by_id(symbol_map, wme_entry.attr_symbol());
        Symbol* value = symbol_by_id(symbol_map, wme_entry.value_symbol());
        if (!id || !attr || !value) continue;

        for (wme* w = thisAgent->all_wmes_in_rete; w != NIL; w = w->rete_next)
        {
            if (w->id == id && w->attr == attr && w->value == value && w->acceptable == wme_entry.acceptable() && w->timetag == wme_entry.timetag())
            {
                detach_restored_wme_from_owner(thisAgent, w);
                remove_wme_from_wm(thisAgent, w);
                break;
            }
        }
    }
}
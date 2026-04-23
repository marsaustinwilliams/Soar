void restore_goal_identity_sets(agent* thisAgent,
                                const google::protobuf::RepeatedPtrField<soar::kernel::GoalIdentitySetState>& goal_identity_sets,
                                const SymbolReverseMap& symbol_map,
                                uint64_t& max_identity_id,
                                uint64_t& max_inst_identity_id)
{
    std::unordered_map<uint64_t, Identity*> identities_by_id;
    std::vector<std::pair<Identity*, RestoredExplanationIdentityState>> identities_to_link;

    for (const auto& goal_identity_state : goal_identity_sets)
    {
        Symbol* goal = symbol_by_id(symbol_map, goal_identity_state.goal_symbol());
        if (!goal || !goal->is_sti())
        {
            continue;
        }

        for (const auto& identity_entry : goal_identity_state.identities())
        {
            Identity* identity = NIL;
            auto existing_identity = identities_by_id.find(identity_entry.identity_id());
            if (existing_identity != identities_by_id.end())
            {
                identity = existing_identity->second;
            }
            else
            {
                thisAgent->memoryManager->allocate_with_pool(MP_identity_sets, &identity);
                identity->init(thisAgent);
                identity->idset_id = identity_entry.identity_id();
                identities_by_id.emplace(identity_entry.identity_id(), identity);
            }

            thisAgent->explanationMemory->add_identity(identity, goal);
            identities_to_link.push_back({identity,
                                          RestoredExplanationIdentityState{identity_entry.identity_id(),
                                                                           identity_entry.joined_identity_id(),
                                                                           identity_entry.chunk_inst_identity(),
                                                                           identity_entry.literalized()}});
            max_identity_id = std::max(max_identity_id, identity_entry.identity_id());
            max_identity_id = std::max(max_identity_id, identity_entry.joined_identity_id());
            max_inst_identity_id = std::max(max_inst_identity_id, identity_entry.chunk_inst_identity());
        }
    }

    for (const auto& link_entry : identities_to_link)
    {
        Identity* identity = link_entry.first;
        const RestoredExplanationIdentityState& restored_state = link_entry.second;
        auto joined_iter = identities_by_id.find(restored_state.joined_identity_id);
        Identity* joined_identity = (joined_iter != identities_by_id.end()) ? joined_iter->second : identity;
        if (!joined_identity)
        {
            joined_identity = identity;
        }

        identity->joined_identity = joined_identity;
        thisAgent->explanationBasedChunker->force_id_to_identity_mapping(restored_state.joined_identity_id,
                                                                         joined_identity);
        if (restored_state.chunk_inst_identity)
        {
            joined_identity->chunk_inst_identity = restored_state.chunk_inst_identity;
        }
        if (identity != joined_identity)
        {
            if (!joined_identity->merged_identities)
            {
                joined_identity->merged_identities = new identity_list();
            }
            joined_identity->merged_identities->push_back(identity);
        }
    }
}

void print_buffered_wme_list(agent* thisAgent, const char* label, cons* head)
{
    std::cout << label << ':';
    if (head == NIL)
    {
        std::cout << " <empty>" << std::endl;
        return;
    }

    for (cons* current = head; current != NIL; current = current->rest)
    {
        wme* buffered_wme = static_cast<wme*>(current->first);
        std::cout << " [tt=" << buffered_wme->timetag
                  << " rete=" << (wme_is_in_rete(thisAgent, buffered_wme) ? 'y' : 'n')
                  << " duplicate=" << (equivalent_wme_exists_in_rete(thisAgent, buffered_wme) ? 'y' : 'n')
                  << " acceptable=" << (buffered_wme->acceptable ? 'y' : 'n')
                  << " id=" << (buffered_wme->id ? buffered_wme->id->to_string() : "nil")
                  << " attr=" << (buffered_wme->attr ? buffered_wme->attr->to_string() : "nil")
                  << " value=" << (buffered_wme->value ? buffered_wme->value->to_string() : "nil")
                  << ']';
    }
    std::cout << std::endl;
}

RestoredWmeOwner determine_live_wme_owner(agent* thisAgent, wme* w)
{
    if (w->acceptable)
    {
        return RestoredWmeOwner::AcceptablePreference;
    }

    slot* s = (w->id && w->attr) ? find_slot(w->id, w->attr) : NIL;
    if (s && wme_is_in_list(s->wmes, w))
    {
        return RestoredWmeOwner::Slot;
    }

    if (w->id && w->id->is_sti() && wme_is_in_list(w->id->id->impasse_wmes, w))
    {
        return RestoredWmeOwner::Impasse;
    }

    if (w->id && w->id->is_sti() && wme_is_in_list(w->id->id->input_wmes, w))
    {
        return RestoredWmeOwner::Input;
    }

    if (w->id && w->id->is_sti() && (w->id->id->isa_goal || w->id->id->isa_impasse))
    {
        return RestoredWmeOwner::Impasse;
    }

    return RestoredWmeOwner::Slot;
}

soar::kernel::WmeOwnerType owner_to_proto_type(RestoredWmeOwner owner)
{
    switch (owner)
    {
        case RestoredWmeOwner::Slot:
            return soar::kernel::WME_OWNER_SLOT;

        case RestoredWmeOwner::AcceptablePreference:
            return soar::kernel::WME_OWNER_ACCEPTABLE;

        case RestoredWmeOwner::Impasse:
            return soar::kernel::WME_OWNER_IMPASSE;

        case RestoredWmeOwner::Input:
            return soar::kernel::WME_OWNER_INPUT;
    }

    return soar::kernel::WME_OWNER_UNKNOWN;
}

RestoredWmeOwner owner_from_proto_type(soar::kernel::WmeOwnerType owner_type,
                                       agent* thisAgent,
                                       const soar::kernel::WmeEntry& wme_entry,
                                       Symbol* id,
                                       Symbol* attr,
                                       preference* pref)
{
    switch (owner_type)
    {
        case soar::kernel::WME_OWNER_SLOT:
            return RestoredWmeOwner::Slot;

        case soar::kernel::WME_OWNER_ACCEPTABLE:
            return RestoredWmeOwner::AcceptablePreference;

        case soar::kernel::WME_OWNER_IMPASSE:
            return RestoredWmeOwner::Impasse;

        case soar::kernel::WME_OWNER_INPUT:
            return RestoredWmeOwner::Input;

        case soar::kernel::WME_OWNER_UNKNOWN:
        default:
            break;
    }

    if (wme_entry.acceptable())
    {
        return RestoredWmeOwner::AcceptablePreference;
    }

    if (!pref)
    {
        if (attr == thisAgent->symbolManager->soarSymbols.io_symbol)
        {
            return RestoredWmeOwner::Input;
        }
        if (id && id->is_sti() && (id->id->isa_goal || id->id->isa_impasse))
        {
            return RestoredWmeOwner::Impasse;
        }
    }

    return RestoredWmeOwner::Slot;
}
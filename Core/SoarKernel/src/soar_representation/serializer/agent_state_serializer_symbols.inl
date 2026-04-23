void patch_loaded_production_rhs_identifiers(agent* thisAgent,
                                             const soar::kernel::AgentState& state,
                                             const SymbolReverseMap& symbol_map)
{
    if (state.production_rhs_identifier_patches_size() == 0)
    {
        return;
    }

    ProductionMap production_map = build_production_name_map(thisAgent);
    for (const auto& patch : state.production_rhs_identifier_patches())
    {
        auto production_it = production_map.find(patch.production_name());
        if (production_it == production_map.end())
        {
            continue;
        }

        action* act = production_it->second->action_list;
        for (uint64_t action_index = 0; act != NIL && action_index < patch.action_index(); ++action_index)
        {
            act = act->next;
        }
        if (!act)
        {
            continue;
        }

        rhs_value* field_ptr = rhs_field_ptr(act, patch.field());
        Symbol* identifier = symbol_by_id(symbol_map, patch.symbol_id());
        if (!field_ptr || !identifier || !identifier->is_sti())
        {
            continue;
        }

        deallocate_rhs_value(thisAgent, *field_ptr);
        *field_ptr = allocate_rhs_value_for_symbol(thisAgent, identifier, 0, 0);
    }
}

Symbol* identifier_symbol_for_entry(agent* thisAgent,
                                    const soar::kernel::SymbolEntry& entry,
                                    bool* owns_reference)
{
    if (!entry.has_identifier()) return NIL;
    char letter = static_cast<char>(entry.identifier().name_letter());
    uint64_t name_number = entry.identifier().name_number();
    goal_stack_level level = static_cast<goal_stack_level>(entry.identifier().level());
    const char* tracked = std::getenv("SOAR_SERIALIZER_TRACK_IDENTIFIER");
    const bool is_tracked = [&]()
    {
        if (!tracked || !tracked[0])
        {
            return false;
        }
        std::string id(1, letter);
        id.append(std::to_string(name_number));
        return (id == tracked);
    }();

    Symbol* sym = thisAgent->symbolManager->find_identifier(letter, name_number);
    const bool found_existing = (sym != NIL);
    if (!sym)
    {
        sym = thisAgent->symbolManager->make_new_identifier(letter, level, name_number, false);
        if (owns_reference)
        {
            *owns_reference = true;
        }
    }
    else
    {
        if (owns_reference)
        {
            *owns_reference = false;
        }
        reset_restored_identifier_runtime_state(sym);
        if (sym->is_sti())
        {
            sym->id->level = level;
            sym->id->promotion_level = level;
        }
    }
    if ((letter >= 'A') && (letter <= 'Z'))
    {
        uint64_t* id_counter = thisAgent->symbolManager->get_id_counter(static_cast<uint64_t>(letter - 'A'));
        if (id_counter && (*id_counter <= name_number))
        {
            *id_counter = name_number + 1;
        }
    }
    if (sym && sym->is_sti())
    {
        sym->id->isa_goal = entry.identifier().isa_goal();
        /* isa_operator is derived from live state ^operator WMEs during WM rebuild. */
        sym->id->isa_operator = 0;
        sym->id->isa_impasse = entry.identifier().isa_impasse();
        sym->id->impasse_type = static_cast<byte>(entry.identifier().impasse_type());

        if (is_tracked)
        {
            std::cerr << "[SERIALIZER_TRACK] restore_symbol id="
                      << letter << name_number
                      << " found_existing=" << (found_existing ? 1 : 0)
                      << " ref=" << sym->reference_count
                      << " level=" << sym->id->level
                      << " isa_goal=" << (sym->id->isa_goal ? 1 : 0)
                      << " isa_operator=" << (sym->id->isa_operator ? 1 : 0)
                      << " isa_impasse=" << (sym->id->isa_impasse ? 1 : 0)
                      << std::endl;
        }
    }
    return sym;
}

Symbol* symbol_for_entry(agent* thisAgent,
                         const soar::kernel::SymbolEntry& entry,
                         bool* owns_reference)
{
    if (owns_reference)
    {
        *owns_reference = false;
    }

    switch (entry.type())
    {
        case soar::kernel::SYMBOL_TYPE_STRING:
            if (owns_reference) *owns_reference = true;
            return thisAgent->symbolManager->make_str_constant(entry.string_value().c_str());
        case soar::kernel::SYMBOL_TYPE_INT:
            if (owns_reference) *owns_reference = true;
            return thisAgent->symbolManager->make_int_constant(entry.int_value());
        case soar::kernel::SYMBOL_TYPE_FLOAT:
            if (owns_reference) *owns_reference = true;
            return thisAgent->symbolManager->make_float_constant(entry.float_value());
        case soar::kernel::SYMBOL_TYPE_IDENTIFIER:
            return identifier_symbol_for_entry(thisAgent, entry, owns_reference);
        case soar::kernel::SYMBOL_TYPE_VARIABLE:
            if (owns_reference) *owns_reference = true;
            return thisAgent->symbolManager->make_variable(entry.variable_name().c_str());
        default:
            return NIL;
    }
}

bool build_symbol_map(agent* thisAgent,
                      const soar::kernel::AgentState& state,
                      SymbolReverseMap& symbol_map,
                      OwnedSymbolRefs& owned_symbol_refs)
{
    for (const auto& entry : state.symbols())
    {
        bool owns_reference = false;
        Symbol* sym = symbol_for_entry(thisAgent, entry, &owns_reference);
        if (!sym && entry.type() != soar::kernel::SYMBOL_TYPE_UNKNOWN)
        {
            return false;
        }
        if (sym)
        {
            symbol_map[entry.symbol_id()] = sym;
            if (owns_reference)
            {
                owned_symbol_refs.push_back(sym);
            }
        }
    }
    return true;
}

void release_owned_symbol_refs(agent* thisAgent, OwnedSymbolRefs& owned_symbol_refs)
{
    const bool debug_owned_release_track = (std::getenv("SOAR_DEBUG_OWNED_RELEASE_TRACK") != nullptr);
    auto should_trace_identifier = [](char letter, uint64_t number)
    {
        return ((letter == 'I') && ((number == 2) || (number == 3))) ||
               ((letter == 'J') && ((number == 1) || (number == 2))) ||
               ((letter == 'O') && (number == 1)) ||
               ((letter == 'O') && (number == 2)) ||
               ((letter == 'O') && (number == 3)) ||
               ((letter == 'S') && ((number == 1) || (number == 2)));
    };

    for (Symbol* sym : owned_symbol_refs)
    {
        const bool is_sti = (sym && sym->is_sti());
        const char letter = is_sti ? sym->id->name_letter : '?';
        const uint64_t number = is_sti ? sym->id->name_number : 0;
        const uint64_t before_ref = sym ? sym->reference_count : 0;
        const bool trace_this = debug_owned_release_track && is_sti && should_trace_identifier(letter, number);

        if (sym && (sym->is_variable() || sym->is_int()))
        {
            if (trace_this)
            {
                std::fprintf(stderr,
                             "restore_owned_release: id=%c%llu action=skip reason=variable_or_int ref=%llu\n",
                             letter,
                             static_cast<unsigned long long>(number),
                             static_cast<unsigned long long>(before_ref));
            }
            continue;
        }

        if (sym && sym->is_sti() && (sym->reference_count <= 1))
        {
            if (trace_this)
            {
                std::fprintf(stderr,
                             "restore_owned_release: id=%c%llu action=skip reason=sti_ref_le_1 ref=%llu\n",
                             letter,
                             static_cast<unsigned long long>(number),
                             static_cast<unsigned long long>(before_ref));
            }
            continue;
        }

        if (sym && sym->is_sti() &&
            ((sym == thisAgent->top_goal) ||
             (sym == thisAgent->bottom_goal) ||
             (sym == thisAgent->top_state) ||
             (sym == thisAgent->active_goal) ||
             (sym == thisAgent->io_header_input) ||
             (sym == thisAgent->io_header_output)))
        {
            if (trace_this)
            {
                std::fprintf(stderr,
                             "restore_owned_release: id=%c%llu action=skip reason=agent_field_anchor ref=%llu\n",
                             letter,
                             static_cast<unsigned long long>(number),
                             static_cast<unsigned long long>(before_ref));
            }
            continue;
        }

        thisAgent->symbolManager->symbol_remove_ref(&sym);
        if (trace_this)
        {
            const uint64_t after_ref = (sym && sym->is_sti()) ? sym->reference_count : 0;
            std::fprintf(stderr,
                         "restore_owned_release: id=%c%llu action=remove before=%llu after=%llu delta=%+lld\n",
                         letter,
                         static_cast<unsigned long long>(number),
                         static_cast<unsigned long long>(before_ref),
                         static_cast<unsigned long long>(after_ref),
                         static_cast<long long>(after_ref) - static_cast<long long>(before_ref));
        }
    }
    owned_symbol_refs.clear();
}

Symbol* symbol_by_id(const SymbolReverseMap& symbol_map, uint64_t id)
{
    if (id == 0) return NIL;
    auto it = symbol_map.find(id);
    return it != symbol_map.end() ? it->second : NIL;
}

Identity* find_goal_identity_by_id(agent* thisAgent, Symbol* goal, uint64_t identity_id)
{
    (void) thisAgent;
    (void) goal;
    (void) identity_id;
    return NIL;
}

Identity* find_identity_by_id(agent* thisAgent, uint64_t identity_id)
{
    (void) thisAgent;
    (void) identity_id;
    return NIL;
}

Identity* get_or_create_restored_identity_by_id(agent* thisAgent, Symbol* goal, uint64_t identity_id)
{
    if (!thisAgent || !goal || !goal->is_sti() || !identity_id)
    {
        return NIL;
    }

    Identity* identity = find_goal_identity_by_id(thisAgent, goal, identity_id);
    if (!identity)
    {
        identity = find_identity_by_id(thisAgent, identity_id);
    }
    if (identity)
    {
        return identity;
    }

    thisAgent->memoryManager->allocate_with_pool(MP_identity_sets, &identity);
    identity->init(thisAgent);
    identity->idset_id = identity_id;
    thisAgent->explanationMemory->add_identity(identity, goal);
    return identity;
}

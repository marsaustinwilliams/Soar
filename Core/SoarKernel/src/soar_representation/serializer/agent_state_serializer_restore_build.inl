void attach_restored_wme_to_owner(agent* thisAgent,
                                  wme* w,
                                  RestoredWmeOwner owner)
{
    switch (owner)
    {
        case RestoredWmeOwner::Slot:
        {
            slot* s = make_slot(thisAgent, w->id, w->attr);
            insert_at_head_of_dll(s->wmes, w, next, prev);
            break;
        }

        case RestoredWmeOwner::AcceptablePreference:
        {
            slot* s = make_slot(thisAgent, w->id, w->attr);
            insert_at_head_of_dll(s->acceptable_preference_wmes, w, next, prev);
            w->value->decider_flag = ALREADY_EXISTING_WME_DECIDER_FLAG;
            w->value->decider_wme = w;
            break;
        }

        case RestoredWmeOwner::Impasse:
            insert_at_head_of_dll(w->id->id->impasse_wmes, w, next, prev);
            break;

        case RestoredWmeOwner::Input:
            insert_at_head_of_dll(w->id->id->input_wmes, w, next, prev);
            break;
    }
}

void detach_restored_wme_from_owner(agent* thisAgent,
                                    wme* w)
{
    switch (determine_live_wme_owner(thisAgent, w))
    {
        case RestoredWmeOwner::Slot:
        {
            slot* s = find_slot(w->id, w->attr);
            if (s)
            {
                remove_from_dll(s->wmes, w, next, prev);
            }
            break;
        }

        case RestoredWmeOwner::AcceptablePreference:
        {
            slot* s = find_slot(w->id, w->attr);
            if (s)
            {
                remove_from_dll(s->acceptable_preference_wmes, w, next, prev);
            }
            if (w->value && (w->value->decider_wme == w))
            {
                w->value->decider_wme = NIL;
                w->value->decider_flag = NOTHING_DECIDER_FLAG;
            }
            break;
        }

        case RestoredWmeOwner::Impasse:
            remove_from_dll(w->id->id->impasse_wmes, w, next, prev);
            break;

        case RestoredWmeOwner::Input:
            remove_from_dll(w->id->id->input_wmes, w, next, prev);
            break;
    }
}

void rebuild_io_header_state(agent* thisAgent)
{
    thisAgent->io_header = NIL;
    thisAgent->io_header_link = NIL;
    thisAgent->io_header_input = NIL;
    thisAgent->io_header_output = NIL;

    if (!thisAgent->top_state)
    {
        thisAgent->prev_top_state = NIL;
        return;
    }

    wme* io_header_wme = find_wme_with_id_attr(thisAgent, thisAgent->top_state, thisAgent->symbolManager->soarSymbols.io_symbol);
    if (io_header_wme && io_header_wme->value && io_header_wme->value->is_sti())
    {
        if (!wme_is_in_list(thisAgent->top_state->id->input_wmes, io_header_wme))
        {
            detach_restored_wme_from_owner(thisAgent, io_header_wme);
            insert_at_head_of_dll(thisAgent->top_state->id->input_wmes, io_header_wme, next, prev);
        }

        thisAgent->io_header_link = io_header_wme;
        thisAgent->io_header = io_header_wme->value;
        thisAgent->symbolManager->symbol_add_ref(thisAgent->io_header);
    }

    if (thisAgent->io_header)
    {
        wme* input_link_wme = find_wme_with_id_attr(thisAgent, thisAgent->io_header, thisAgent->symbolManager->soarSymbols.input_link_symbol);
        if (input_link_wme)
        {
            if (!wme_is_in_list(thisAgent->io_header->id->input_wmes, input_link_wme))
            {
                detach_restored_wme_from_owner(thisAgent, input_link_wme);
                insert_at_head_of_dll(thisAgent->io_header->id->input_wmes, input_link_wme, next, prev);
            }

            thisAgent->io_header_input = input_link_wme->value;
        }

        wme* output_link_wme = find_wme_with_id_attr(thisAgent, thisAgent->io_header, thisAgent->symbolManager->soarSymbols.output_link_symbol);
        if (output_link_wme)
        {
            if (!wme_is_in_list(thisAgent->io_header->id->input_wmes, output_link_wme))
            {
                detach_restored_wme_from_owner(thisAgent, output_link_wme);
                insert_at_head_of_dll(thisAgent->io_header->id->input_wmes, output_link_wme, next, prev);
            }

            thisAgent->io_header_output = output_link_wme->value;
        }
    }

    thisAgent->prev_top_state = thisAgent->top_state;
}

void rebuild_output_link_state(agent* thisAgent,
                               const google::protobuf::RepeatedPtrField<soar::kernel::WmeEntry>& entries)
{
    std::unordered_set<uint64_t> output_link_timetags;
    for (const auto& wme_entry : entries)
    {
        if (wme_entry.output_link())
        {
            output_link_timetags.insert(wme_entry.timetag());
        }
    }

    if (output_link_timetags.empty())
    {
        thisAgent->existing_output_links = NIL;
        thisAgent->output_link_for_tc = NIL;
        thisAgent->output_link_tc_num = 0;
        thisAgent->collected_io_wmes = NIL;
        thisAgent->output_link_changed = false;
        return;
    }

    constexpr size_t kRestoredLinkNameSize = 1024;

    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        if (!output_link_timetags.count(current->timetag) || current->output_link)
        {
            continue;
        }

        output_link* restored_output = NIL;
        thisAgent->memoryManager->allocate_with_pool(MP_output_link, &restored_output);
        insert_at_head_of_dll(thisAgent->existing_output_links, restored_output, next, prev);

        char link_name[kRestoredLinkNameSize];
        current->attr->to_string(false, false, link_name, kRestoredLinkNameSize);

        restored_output->status = UNCHANGED_OL_STATUS;
        restored_output->link_wme = current;
        restored_output->ids_in_tc = NIL;
        restored_output->cb = soar_exists_callback_id(thisAgent, OUTPUT_PHASE_CALLBACK, link_name);

        current->output_link = restored_output;
        wme_add_ref(current, true);
    }

    /* Rebuild TC ownership metadata so identifier associated_output_links
       matches live runtime state immediately after restore. */
    for (output_link* ol = thisAgent->existing_output_links; ol != NIL; ol = ol->next)
    {
        calculate_output_link_tc_info(thisAgent, ol);
    }

    thisAgent->output_link_for_tc = NIL;
    thisAgent->collected_io_wmes = NIL;
    thisAgent->output_link_changed = false;
}

void rebuild_output_link_transient_state_from_tc(agent* thisAgent)
{
    thisAgent->output_link_for_tc = NIL;
    output_link* fallback_output_link = NIL;

    if (thisAgent->collected_io_wmes)
    {
        deallocate_io_wme_list(thisAgent, thisAgent->collected_io_wmes);
        thisAgent->collected_io_wmes = NIL;
    }

    if (!thisAgent->existing_output_links || !thisAgent->output_link_tc_num)
    {
        return;
    }

    for (output_link* ol = thisAgent->existing_output_links; ol != NIL; ol = ol->next)
    {
        if (!ol->link_wme || !ol->link_wme->value || !ol->link_wme->value->is_sti())
        {
            continue;
        }

        if (!fallback_output_link)
        {
            fallback_output_link = ol;
        }

        if (ol->link_wme->value->id->tc_num != thisAgent->output_link_tc_num)
        {
            continue;
        }

        thisAgent->output_link_for_tc = ol;
        thisAgent->collected_io_wmes = get_io_wmes_for_output_link(thisAgent, ol);
        return;
    }

    if (fallback_output_link)
    {
        thisAgent->output_link_for_tc = fallback_output_link;
        thisAgent->collected_io_wmes = get_io_wmes_for_output_link(thisAgent, fallback_output_link);
    }
}

void synchronize_identifier_counters_from_live_state(agent* thisAgent)
{
    auto bump_counter = [&](Symbol* sym)
    {
        if (!sym || !sym->is_sti())
        {
            return;
        }

        const char letter = sym->id->name_letter;
        if ((letter < 'A') || (letter > 'Z'))
        {
            return;
        }

        uint64_t* id_counter = thisAgent->symbolManager->get_id_counter(static_cast<uint64_t>(letter - 'A'));
        if (id_counter && (*id_counter <= sym->id->name_number))
        {
            *id_counter = sym->id->name_number + 1;
        }
    };

    for (wme* current = thisAgent->all_wmes_in_rete; current != NIL; current = current->rete_next)
    {
        bump_counter(current->id);
        bump_counter(current->value);
    }

    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        bump_counter(goal);
    }

    bump_counter(thisAgent->top_state);
    bump_counter(thisAgent->io_header);
    bump_counter(thisAgent->io_header_input);
    bump_counter(thisAgent->io_header_output);
}

void balance_restored_identifier_construction_refs(agent* thisAgent)
{
    for (uint64_t letter_index = 0; letter_index < 26; ++letter_index)
    {
        const char letter = static_cast<char>('A' + letter_index);
        const uint64_t max_number = *thisAgent->symbolManager->get_id_counter(letter_index);
        for (uint64_t number = 1; number < max_number; ++number)
        {
            Symbol* sym = thisAgent->symbolManager->find_identifier(letter, number);
            if (!sym || !sym->is_sti())
            {
                continue;
            }

            if (sym->reference_count > 1)
            {
                Symbol* tmp = sym;
                thisAgent->symbolManager->symbol_remove_ref(&tmp);
            }
        }
    }
}

void rebuild_goal_module_wme_state(agent* thisAgent)
{
    for (Symbol* goal = thisAgent->top_goal; goal != NIL; goal = goal->id->lower_goal)
    {
        if (!goal->is_sti())
        {
            continue;
        }

        if (goal->id->rl_info)
        {
            goal->id->rl_info->rl_link_wme = find_wme_with_id_attr(thisAgent,
                                                                   goal,
                                                                   thisAgent->symbolManager->soarSymbols.rl_sym_reward_link);
        }

        if (goal->id->epmem_info)
        {
            goal->id->epmem_info->epmem_link_wme = find_wme_with_id_attr(thisAgent,
                                                                         goal,
                                                                         thisAgent->symbolManager->soarSymbols.epmem_sym);
            goal->id->epmem_info->cmd_wme = NIL;
            goal->id->epmem_info->result_wme = NIL;
            goal->id->epmem_info->epmem_time_wme = NIL;

            if (goal->id->epmem_info->epmem_link_wme &&
                goal->id->epmem_info->epmem_link_wme->value &&
                goal->id->epmem_info->epmem_link_wme->value->is_sti())
            {
                Symbol* epmem_header = goal->id->epmem_info->epmem_link_wme->value;
                goal->id->epmem_info->cmd_wme = find_wme_with_id_attr(thisAgent,
                                                                      epmem_header,
                                                                      thisAgent->symbolManager->soarSymbols.epmem_sym_cmd);
                goal->id->epmem_info->result_wme = find_wme_with_id_attr(thisAgent,
                                                                         epmem_header,
                                                                         thisAgent->symbolManager->soarSymbols.epmem_sym_result);
                goal->id->epmem_info->epmem_time_wme = find_wme_with_id_attr(thisAgent,
                                                                             epmem_header,
                                                                             thisAgent->symbolManager->soarSymbols.epmem_sym_present_id);
            }
        }

        if (goal->id->smem_info)
        {
            goal->id->smem_info->smem_link_wme = find_wme_with_id_attr(thisAgent,
                                                                       goal,
                                                                       thisAgent->symbolManager->soarSymbols.smem_sym);
            goal->id->smem_info->cmd_wme = NIL;
            goal->id->smem_info->result_wme = NIL;

            if (goal->id->smem_info->smem_link_wme &&
                goal->id->smem_info->smem_link_wme->value &&
                goal->id->smem_info->smem_link_wme->value->is_sti())
            {
                Symbol* smem_header = goal->id->smem_info->smem_link_wme->value;
                goal->id->smem_info->cmd_wme = find_wme_with_id_attr(thisAgent,
                                                                     smem_header,
                                                                     thisAgent->symbolManager->soarSymbols.smem_sym_cmd);
                goal->id->smem_info->result_wme = find_wme_with_id_attr(thisAgent,
                                                                        smem_header,
                                                                        thisAgent->symbolManager->soarSymbols.smem_sym_result);
            }
        }
    }
}

void initialize_restored_goal(agent* thisAgent, Symbol* goal)
{
    if (!goal || !goal->is_sti() || !(goal->id->isa_goal || goal->id->isa_impasse))
    {
        return;
    }

    bool skip_goal_post_link_addition = false;
    if (const char* skip_goal_post_link_env = std::getenv("SOAR_RESTORE_SKIP_GOAL_POST_LINK_ADDITION"))
    {
        if ((skip_goal_post_link_env[0] == '1') ||
            !std::strcmp(skip_goal_post_link_env, "true") ||
            !std::strcmp(skip_goal_post_link_env, "on"))
        {
            skip_goal_post_link_addition = true;
        }
    }

    if (!skip_goal_post_link_addition && (goal->id->link_count == 0))
    {
        post_link_addition(thisAgent, NIL, goal);
    }

    if (!goal->id->operator_slot)
    {
        goal->id->operator_slot = make_slot(thisAgent, goal, thisAgent->symbolManager->soarSymbols.operator_symbol);
    }
    goal->id->allow_bottom_up_chunks = true;
    goal->id->gds = NIL;
    goal->id->ms_o_assertions = NIL;
    goal->id->ms_i_assertions = NIL;
    goal->id->ms_retractions = NIL;
    goal->id->preferences_from_goal = NIL;
    goal->id->input_wmes = NIL;
    if (!goal->id->rl_info)
    {
        thisAgent->memoryManager->allocate_with_pool(MP_rl_info, &(goal->id->rl_info));
        goal->id->rl_info->previous_q = 0;
        goal->id->rl_info->reward = 0;
        goal->id->rl_info->rho = 1.0;
        goal->id->rl_info->gap_age = 0;
        goal->id->rl_info->hrl_age = 0;
        thisAgent->memoryManager->allocate_with_pool(MP_rl_et, &(goal->id->rl_info->eligibility_traces));
        goal->id->rl_info->eligibility_traces = new(goal->id->rl_info->eligibility_traces) rl_et_map();
        thisAgent->memoryManager->allocate_with_pool(MP_rl_rule, &(goal->id->rl_info->prev_op_rl_rules));
        goal->id->rl_info->prev_op_rl_rules = new(goal->id->rl_info->prev_op_rl_rules) production_list();
    }
    goal->id->rl_trace = &thisAgent->RL->rl_trace[goal->id->level];

    if (!goal->id->epmem_info)
    {
        thisAgent->memoryManager->allocate_with_pool(MP_epmem_info, &(goal->id->epmem_info));
        goal->id->epmem_info->last_ol_time = 0;
        goal->id->epmem_info->last_ol_count = 0;
        goal->id->epmem_info->last_cmd_time = 0;
        goal->id->epmem_info->last_cmd_count = 0;
        goal->id->epmem_info->last_memory = EPMEM_MEMID_NONE;
        goal->id->epmem_info->epmem_link_wme = NIL;
        goal->id->epmem_info->cmd_wme = NIL;
        goal->id->epmem_info->result_wme = NIL;
        goal->id->epmem_info->epmem_time_wme = NIL;
        thisAgent->memoryManager->allocate_with_pool(MP_epmem_wmes, &(goal->id->epmem_info->epmem_wmes));
        goal->id->epmem_info->epmem_wmes = new(goal->id->epmem_info->epmem_wmes) preference_list();
    }

    if (!goal->id->smem_info)
    {
        thisAgent->memoryManager->allocate_with_pool(MP_smem_info, &(goal->id->smem_info));
        goal->id->smem_info->last_cmd_time[0] = 0;
        goal->id->smem_info->last_cmd_time[1] = 0;
        goal->id->smem_info->last_cmd_count[0] = 0;
        goal->id->smem_info->last_cmd_count[1] = 0;
        goal->id->smem_info->smem_link_wme = NIL;
        goal->id->smem_info->cmd_wme = NIL;
        goal->id->smem_info->result_wme = NIL;
        thisAgent->memoryManager->allocate_with_pool(MP_smem_wmes, &(goal->id->smem_info->smem_wmes));
        goal->id->smem_info->smem_wmes = new(goal->id->smem_info->smem_wmes) preference_list();
    }
}

void rebuild_goal_chain(agent* thisAgent,
                        const soar::kernel::AgentState& state,
                        const SymbolReverseMap& symbol_map)
{
    std::vector<Symbol*> goal_chain;
    for (const auto& entry : symbol_map)
    {
        Symbol* sym = entry.second;
        if (sym && sym->is_sti() && (sym->id->isa_goal || sym->id->isa_impasse))
        {
            sym->id->higher_goal = NIL;
            sym->id->lower_goal = NIL;
            initialize_restored_goal(thisAgent, sym);
            goal_chain.push_back(sym);
        }
    }

    std::sort(goal_chain.begin(), goal_chain.end(), [](const Symbol* a, const Symbol* b)
    {
        return a->id->level < b->id->level;
    });

    for (size_t i = 0; i < goal_chain.size(); ++i)
    {
        Symbol* goal = goal_chain[i];
        goal->id->higher_goal = (i == 0 ? NIL : goal_chain[i - 1]);
        goal->id->lower_goal = (i + 1 < goal_chain.size() ? goal_chain[i + 1] : NIL);
    }

    if (!goal_chain.empty())
    {
        thisAgent->top_goal = goal_chain.front();
        thisAgent->bottom_goal = goal_chain.back();
    }
    else
    {
        thisAgent->top_goal = NIL;
        thisAgent->bottom_goal = NIL;
    }

    if (state.settings().top_state_symbol() != 0)
    {
        thisAgent->top_state = symbol_by_id(symbol_map, state.settings().top_state_symbol());
    }
}

void set_agent_symbol_fields(agent* thisAgent,
                             const soar::kernel::AgentState& state,
                             const SymbolReverseMap& symbol_map)
{
    const auto& settings = state.settings();
    thisAgent->current_phase = static_cast<top_level_phase>(settings.current_phase());
    thisAgent->stop_soar = settings.stop_soar();
    thisAgent->system_halted = settings.system_halted();
    thisAgent->go_number = static_cast<int>(settings.go_number());
    if (settings.go_slot_attr_symbol() != 0)
    {
        thisAgent->go_slot_attr = symbol_by_id(symbol_map, settings.go_slot_attr_symbol());
    }
    thisAgent->go_slot_level = static_cast<goal_stack_level>(settings.go_slot_level());
    thisAgent->go_type = static_cast<go_type_enum>(settings.go_type());
    thisAgent->input_period = settings.input_period();
    thisAgent->input_cycle_flag = settings.input_cycle_flag();
    thisAgent->current_wme_timetag = settings.current_wme_timetag();
    thisAgent->bottom_goal = symbol_by_id(symbol_map, settings.bottom_goal_symbol());
    thisAgent->top_goal = symbol_by_id(symbol_map, settings.top_goal_symbol());
    thisAgent->top_state = symbol_by_id(symbol_map, settings.top_state_symbol());
    thisAgent->reason_for_stopping = settings.reason_for_stopping().empty() ? "" : settings.reason_for_stopping().c_str();

    thisAgent->name_of_production_being_reordered = NIL;
    if (!settings.name_of_production_being_reordered().empty())
    {
        Symbol* prod_name_sym = thisAgent->symbolManager->find_str_constant(
            settings.name_of_production_being_reordered().c_str());
        if (prod_name_sym && prod_name_sym->is_string())
        {
            thisAgent->name_of_production_being_reordered = prod_name_sym->sc->name;
        }
    }
}

void apply_state_counters(agent* thisAgent,
                          const soar::kernel::AgentState& state)
{
    const auto& counters = state.counters();
    const uint64_t live_existing_wmes = thisAgent->num_existing_wmes;
    thisAgent->cumulative_wm_size = counters.cumulative_wm_size();
    thisAgent->num_wm_sizes_accumulated = counters.num_wm_sizes_accumulated();
    thisAgent->max_wm_size = counters.max_wm_size();
    thisAgent->wme_addition_count = counters.wme_addition_count();
    thisAgent->wme_removal_count = counters.wme_removal_count();
    thisAgent->init_count = counters.init_count();
    thisAgent->d_cycle_count = counters.d_cycle_count();
    thisAgent->WM->wma_d_cycle_count = counters.d_cycle_count();
    thisAgent->e_cycle_count = counters.e_cycle_count();
    thisAgent->e_cycles_this_d_cycle = counters.e_cycles_this_d_cycle();
    thisAgent->num_existing_wmes = live_existing_wmes;
    thisAgent->production_firing_count = counters.production_firing_count();
    thisAgent->start_dc_production_firing_count = counters.start_dc_production_firing_count();
    thisAgent->start_dc_wme_addition_count = counters.start_dc_wme_addition_count();
    thisAgent->start_dc_wme_removal_count = counters.start_dc_wme_removal_count();
    thisAgent->max_dc_production_firing_count_value = counters.max_dc_production_firing_count_value();
    thisAgent->max_dc_production_firing_count_cycle = counters.max_dc_production_firing_count_cycle();
    thisAgent->max_dc_wm_changes_value = counters.max_dc_wm_changes_value();
    thisAgent->max_dc_wm_changes_cycle = counters.max_dc_wm_changes_cycle();
    thisAgent->d_cycle_last_output = counters.d_cycle_last_output();
    thisAgent->decide_phases_count = counters.decide_phases_count();
    thisAgent->run_phase_count = counters.run_phase_count();
    thisAgent->run_elaboration_count = counters.run_elaboration_count();
    thisAgent->run_last_output_count = counters.run_last_output_count();
    thisAgent->run_generated_output_count = counters.run_generated_output_count();
    thisAgent->pe_cycle_count = counters.pe_cycle_count();
    thisAgent->pe_cycles_this_d_cycle = counters.pe_cycles_this_d_cycle();
    thisAgent->inner_e_cycle_count = counters.inner_e_cycle_count();

    if (counters.serializer_runtime_parity_v1())
    {
        const uint64_t snapshot_raw_time = counters.timers_snapshot_raw_time();
        const uint64_t restore_raw_time = get_raw_time();
        auto rebase_timer_start = [&](const soar::kernel::TimerSnapshot& timer_snapshot) -> uint64_t
        {
            const uint64_t saved_t1 = timer_snapshot.t1();
            if (!timer_snapshot.running() || (snapshot_raw_time == 0) || (restore_raw_time <= snapshot_raw_time))
            {
                return saved_t1;
            }

            if (saved_t1 >= snapshot_raw_time)
            {
                return restore_raw_time;
            }

            const uint64_t elapsed_at_save = snapshot_raw_time - saved_t1;
            return (restore_raw_time > elapsed_at_save) ? (restore_raw_time - elapsed_at_save) : 0;
        };

        thisAgent->current_retesave_amindex = counters.current_retesave_amindex();
        thisAgent->reteload_num_ams = counters.reteload_num_ams();
        thisAgent->current_retesave_symindex = counters.current_retesave_symindex();
        thisAgent->reteload_num_syms = counters.reteload_num_syms();
        thisAgent->alpha_mem_id_counter = static_cast<uint32_t>(counters.alpha_mem_id_counter());
        thisAgent->beta_node_id_counter = static_cast<uint32_t>(counters.beta_node_id_counter());
        thisAgent->current_tc_number = static_cast<tc_number>(counters.current_tc_number());

        if (counters.has_timers_cpu())
        {
            thisAgent->timers_cpu.import_state(rebase_timer_start(counters.timers_cpu()),
                                               counters.timers_cpu().elapsed(),
                                               counters.timers_cpu().raw_per_usec(),
                                               counters.timers_cpu().running());
        }

        if (counters.has_timers_kernel())
        {
            thisAgent->timers_kernel.import_state(rebase_timer_start(counters.timers_kernel()),
                                                  counters.timers_kernel().elapsed(),
                                                  counters.timers_kernel().raw_per_usec(),
                                                  counters.timers_kernel().running());
        }

        if (counters.has_timers_phase())
        {
            thisAgent->timers_phase.import_state(rebase_timer_start(counters.timers_phase()),
                                                 counters.timers_phase().elapsed(),
                                                 counters.timers_phase().raw_per_usec(),
                                                 counters.timers_phase().running());
        }

        if (counters.has_timers_total_cpu_time())
        {
            thisAgent->timers_total_cpu_time.set_usec(counters.timers_total_cpu_time().total());
        }
        if (counters.has_timers_total_kernel_time())
        {
            thisAgent->timers_total_kernel_time.set_usec(counters.timers_total_kernel_time().total());
        }

        const int phase_count = (counters.timers_decision_cycle_phase_usec_size() < NUM_PHASE_TYPES)
            ? counters.timers_decision_cycle_phase_usec_size()
            : NUM_PHASE_TYPES;
        for (int i = 0; i < phase_count; ++i)
        {
            thisAgent->timers_decision_cycle_phase[i].set_usec(counters.timers_decision_cycle_phase_usec(i));
        }

        thisAgent->timers_input_function_cpu_time.set_usec(counters.timers_input_function_cpu_time_usec());
        thisAgent->timers_output_function_cpu_time.set_usec(counters.timers_output_function_cpu_time_usec());

        const int callback_count = (counters.callback_timers_usec_size() < NUMBER_OF_CALLBACKS)
            ? counters.callback_timers_usec_size()
            : NUMBER_OF_CALLBACKS;
        for (int i = 0; i < callback_count; ++i)
        {
            thisAgent->callback_timers[i].set_usec(counters.callback_timers_usec(i));
        }

        const int monitors_count = (counters.timers_monitors_cpu_time_usec_size() < NUM_PHASE_TYPES)
            ? counters.timers_monitors_cpu_time_usec_size()
            : NUM_PHASE_TYPES;
        for (int i = 0; i < monitors_count; ++i)
        {
            thisAgent->timers_monitors_cpu_time[i].set_usec(counters.timers_monitors_cpu_time_usec(i));
        }

        thisAgent->last_derived_kernel_time_usec = counters.last_derived_kernel_time_usec();
        thisAgent->max_dc_time_usec = counters.max_dc_time_usec();
        thisAgent->max_dc_time_cycle = counters.max_dc_time_cycle();
        thisAgent->max_dc_epmem_time_sec = counters.max_dc_epmem_time_sec();
        thisAgent->total_dc_epmem_time_sec = counters.total_dc_epmem_time_sec();
        thisAgent->max_dc_epmem_time_cycle = counters.max_dc_epmem_time_cycle();
        thisAgent->max_dc_smem_time_sec = counters.max_dc_smem_time_sec();
        thisAgent->total_dc_smem_time_sec = counters.total_dc_smem_time_sec();
        thisAgent->max_dc_smem_time_cycle = counters.max_dc_smem_time_cycle();

        thisAgent->tf_printing_tc = static_cast<tc_number>(counters.tf_printing_tc());
        thisAgent->output_link_tc_num = static_cast<tc_number>(counters.output_link_tc_num());
    }
}

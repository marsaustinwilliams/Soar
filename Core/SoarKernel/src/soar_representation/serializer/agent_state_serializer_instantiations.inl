instantiation* create_refracted_instantiation(agent* thisAgent,
                                              production* prod,
                                              token* tok,
                                              wme* w,
                                              Symbol* match_goal,
                                              goal_stack_level match_level,
                                              bool in_ms = true,
                                              bool clear_identity_maps = true)
{
    instantiation* inst = NIL;
    action* rhs_vars = NIL;

    ExplainTraceType ebcTraceType = WM_Trace;

    init_instantiation(thisAgent, inst, thisAgent->symbolManager->soarSymbols.architecture_inst_symbol, prod, tok, w);
    inst->in_ms = in_ms;
    inst->match_goal = match_goal;
    inst->match_goal_level = match_level;

    if (prod->type == TEMPLATE_PRODUCTION_TYPE)
    {
        ebcTraceType = WM_Trace_w_Inequalities;
    }
    else if ((match_level > TOP_GOAL_LEVEL) && thisAgent->explanationBasedChunker->ebc_settings[SETTING_EBC_LEARNING_ON])
    {
        ebcTraceType = Explanation_Trace;
    }

    p_node_to_conditions_and_rhs(thisAgent, prod->p_node, tok, w,
                                 &(inst->top_of_instantiated_conditions),
                                 &(inst->bottom_of_instantiated_conditions),
                                 (ebcTraceType != WM_Trace) ? &rhs_vars : NIL,
                                 ebcTraceType);

    for (condition* cond = inst->top_of_instantiated_conditions; cond != NIL; cond = cond->next)
    {
        if (cond->type == POSITIVE_CONDITION)
        {
            cond->bt.level = cond->bt.wme_->id->id->level;
            cond->bt.trace = cond->bt.wme_->preference;
        }
    }

    finalize_instantiation(thisAgent, inst, false, NIL, false);

    if (clear_identity_maps && (match_level > TOP_GOAL_LEVEL) && thisAgent->explanationBasedChunker->ebc_settings[SETTING_EBC_LEARNING_ON])
    {
        thisAgent->explanationBasedChunker->clear_id_to_identity_map();
    }
    else if (clear_identity_maps && (prod->type != TEMPLATE_PRODUCTION_TYPE))
    {
        thisAgent->explanationBasedChunker->clear_symbol_identity_map();
    }

    if (rhs_vars)
    {
        deallocate_action_list(thisAgent, rhs_vars);
    }

    if (in_ms)
    {
        insert_at_head_of_dll(prod->instantiations, inst, next, prev);
    }
    return inst;
}

void reassign_restored_preference_instantiation(agent* thisAgent,
                                                preference* pref,
                                                instantiation* inst,
                                                SavedInstantiationSet* cleaned_previous_insts)
{
    if (!pref || !inst || (pref->inst == inst))
    {
        return;
    }

    instantiation* previous_inst = pref->inst;
    const bool relink_goal_list = pref->on_goal_list && previous_inst && (previous_inst->match_goal != inst->match_goal);

    if (relink_goal_list && previous_inst && previous_inst->match_goal)
    {
        remove_from_dll(previous_inst->match_goal->id->preferences_from_goal, pref, all_of_goal_next, all_of_goal_prev);
        pref->on_goal_list = false;
    }

    if (previous_inst)
    {
        remove_from_dll(previous_inst->preferences_generated, pref, inst_next, inst_prev);
    }

    pref->inst = inst;
    pref->level = inst->match_goal_level;
    insert_at_head_of_dll(inst->preferences_generated, pref, inst_next, inst_prev);

    if (!pref->on_goal_list && inst->match_goal)
    {
        insert_at_head_of_dll(inst->match_goal->id->preferences_from_goal, pref, all_of_goal_next, all_of_goal_prev);
        pref->on_goal_list = true;
    }

    if (inst->match_goal)
    {
        rebind_restored_preference_identities(thisAgent, pref, inst->match_goal);
    }

    if (previous_inst && !previous_inst->in_ms && !previous_inst->preferences_generated)
    {
        if (cleaned_previous_insts && !cleaned_previous_insts->insert(previous_inst).second)
        {
            return;
        }
        deallocate_instantiation(thisAgent, previous_inst);
    }
}
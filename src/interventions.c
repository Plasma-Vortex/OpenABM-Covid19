/*
 * interventions.c
 *
 *  Created on: 18 Mar 2020
 *      Author: hinchr
 */

#include "model.h"
#include "individual.h"
#include "utilities.h"
#include "constant.h"
#include "params.h"
#include "network.h"
#include "disease.h"
#include "interventions.h"
#include "hashset.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include <assert.h>

/*****************************************************************************************
*  Name:		set_up_transition_times
*  Description: sets up discrete distributions for the times it takes to
*  				transition along edges of the intervention
*
*  Returns:		void
******************************************************************************************/
void set_up_transition_times_intervention( model *model )
{
	parameters *params = model->params;
	int **transitions  = model->transition_time_distributions;

	geometric_max_draw_list( transitions[SYMPTOMATIC_QUARANTINE], 	  N_DRAW_LIST, params->quarantine_dropout_self,            params->quarantine_length_self );
	geometric_max_draw_list( transitions[TRACED_QUARANTINE_SYMPTOMS], N_DRAW_LIST, params->quarantine_dropout_traced_symptoms, params->quarantine_length_traced_symptoms );
	geometric_max_draw_list( transitions[TRACED_QUARANTINE_POSITIVE], N_DRAW_LIST, params->quarantine_dropout_traced_positive, params->quarantine_length_traced_positive );
	geometric_max_draw_list( transitions[TEST_RESULT_QUARANTINE],     N_DRAW_LIST, params->quarantine_dropout_positive,        params->quarantine_length_positive );
}

/*****************************************************************************************
*  Name:		set_up_app_users
*  Description: Set up the proportion of app users in the population (default is FALSE)
******************************************************************************************/
void set_up_app_users( model *model )
{
	long idx, jdx, age, current_users, not_users, max_user;
	double *fraction = model->params->app_users_fraction;

	if ( model->params->household_app_adoption )
	{
		directory *dir = model->household_directory;
		long n_users = 0;

		long *permutation = calloc( dir->n_idx, sizeof( long ) );
		for( idx = 0; idx < dir->n_idx; idx++)
			permutation[idx] = idx;
		gsl_ran_shuffle( rng, permutation, dir->n_idx, sizeof( long ) );

		for( idx = 0; idx < dir->n_idx; idx++)
		{
			long *members = dir->val[idx];
			double prob = 0;
			for( jdx = 0; jdx < dir->n_jdx[idx]; jdx++ )
			{
				individual *member = &(model->population[members[jdx]]);
				prob += fraction[member->age_group];
			}
			prob /= dir->n_jdx[idx];

			if (permutation[idx] < prob * dir->n_idx)
				for( jdx = 0; jdx < dir->n_jdx[idx]; jdx++ )
				{
					individual *member = &(model->population[members[jdx]]);
					member->app_user = TRUE;
					n_users++;
				}
		}

		if( model->params->cluster_app_adoption )
		{
			hashset **adj = calloc(dir->n_idx, sizeof(hashset*));
			for( idx = 0; idx < dir->n_idx; idx++ )
				adj[idx] = init_set();

			network *net;
			for( int j = 0; j < model->n_occupation_networks; j++ )
			{
				net = model->occupation_network[j];

				for( long i = 0; i < net->n_edges; i++ )
				{
					long a = net->edges[i].id1;
					long b = net->edges[i].id2;
					long house_a = model->population[a].house_no;
					long house_b = model->population[b].house_no;
					if (house_a == house_b)
						continue;
					set_insert(adj[house_a], house_b);
					set_insert(adj[house_b], house_a);
				}
			}
			double target = 0, sum = 0;
			for( age = 0; age < N_AGE_GROUPS; age++ )
			{
				target += fraction[age] * model->params->population[age];
				sum += model->params->population[age];
			}
			target /= sum;

			if (DEBUG) {
				printf("target = %lf, real = %lf\n", target, 1.0*n_users/model->params->n_total);
			}
			
			for( int i = 0; i < N_CLUSTER_ITERATIONS; i++ )
			{
				int n_flip = 0;
				if (DEBUG) {
					printf("before i = %d, real app adoption = %lf\n", i, 1.0*n_users/model->params->n_total);
				}
				for( idx = 0; idx < dir->n_idx; idx++ )
				{
					double real_fraction = 1.0*n_users/model->params->n_total;
					double adj_fraction = 0;
					for( int j = 0; j < adj[idx]->n_data; j++ )
						if( adj[idx]->data[j].shift >= 0 )
						{
							long house = adj[idx]->data[j].key;
							jdx = dir->val[house][0];
							adj_fraction += model->population[jdx].app_user;
						}
					if( !set_empty(adj[idx]) )
						adj_fraction /= set_size(adj[idx]);

					double cutoff;
					if( real_fraction < target )
						cutoff = real_fraction/2;
					else
						cutoff = 1 - (1-real_fraction)/2;
					//printf("adj_f = %lf, lo = %lf, hi = %lf, rf = %lf\n", adj_fraction, lo, hi, real_fraction);

					if( adj_fraction < min(cutoff, 0.5) && model->population[dir->val[idx][0]].app_user )
					{
						for( jdx = 0; jdx < dir->n_jdx[idx]; jdx++ )
						{
							model->population[dir->val[idx][jdx]].app_user = FALSE;
							n_users--;
						}
						n_flip++;
					}
					else if( adj_fraction > max(cutoff, 0.5) && !model->population[dir->val[idx][0]].app_user )
					{
						for( jdx = 0; jdx < dir->n_jdx[idx]; jdx++ )
						{
							model->population[dir->val[idx][jdx]].app_user = TRUE;
							n_users++;
						}
						n_flip++;
					}
				}
				if (DEBUG) {
					printf("n_flip = %d, h_total = %ld\n", n_flip, dir->n_idx);
				}
			}

			for( idx = 0; idx < dir->n_idx; idx++ )
				destroy_set(adj[idx]);
			free(adj);
		}

	}
	else
	{
		for( age = 0; age < N_AGE_GROUPS; age++ )
		{
			current_users = 0;
			not_users     = 0;
			for( idx = 0; idx < model->params->n_total; idx++ )
				if( model->population[ idx ].age_group == age )
				{
					current_users += model->population[ idx ].app_user;
					not_users     += 1 - model->population[ idx ].app_user;
				}

			if( ( current_users + not_users) == 0 )
				continue;

			max_user = ceil( ( current_users + not_users ) * fraction[age] ) - current_users;
			if( max_user < 0 || max_user > not_users )
				print_exit( "Bad target app_fraction_users" );

			int *users = calloc( not_users, sizeof( int ) );

			for( idx = 0; idx < max_user; idx++ )
				users[ idx ] = 1;

			gsl_ran_shuffle( rng, users, not_users, sizeof( int ) );

			jdx   = 0;
			for( idx = 0; idx < model->params->n_total; idx++ )
				if( model->population[ idx ].age_group == age && model->population[ idx ].app_user == FALSE )
					model->population[ idx ].app_user = users[ jdx++ ];

			free( users );
		}

		if( model->params->cluster_app_adoption )
		{
			hashset **adj = calloc(model->params->n_total, sizeof(hashset*));
			for( idx = 0; idx < model->params->n_total; idx++ )
				adj[idx] = init_set();

			network *net;
			for( int j = 0; j <= model->n_occupation_networks; j++ )
			{
				if (j < model->n_occupation_networks)
					net = model->occupation_network[j];
				else
					net = model->household_network;

				for( long i = 0; i < net->n_edges; i++ )
				{
					long a = net->edges[i].id1;
					long b = net->edges[i].id2;
					set_insert(adj[a], b);
					set_insert(adj[b], a);
				}
			}

			long real_n_users[N_AGE_GROUPS];
			long total_pop[N_AGE_GROUPS];
			for( age = 0; age < N_AGE_GROUPS; age++ )
			{
				real_n_users[age] = 0;
				total_pop[age] = 0;
				for( idx = 0; idx < model->params->n_total; idx++ )
					if( model->population[ idx ].age_group == age )
					{
						real_n_users[age] += model->population[ idx ].app_user;
						total_pop[age]++;
					}
				if (DEBUG) printf("age = %d, target = %lf, real = %lf\n", (int)age, fraction[age], 1.0*real_n_users[age]/total_pop[age]);
			}

			for( int i = 0; i < N_CLUSTER_ITERATIONS; i++ )
			{
				int n_flips = 0;
				if (DEBUG) {
					printf("before i = %d\n", i);
					double cnt = 0;
					for (int i = 0; i<model->params->n_total; i++)
						cnt += model->population[i].app_user;
					printf("real app adoption = %lf\n", cnt/model->params->n_total);
				}
				for( idx = 0; idx < model->params->n_total; idx++ )
				{
					age = model->population[idx].age_group;
					double real_fraction = 1.0*real_n_users[age]/total_pop[age];
					double adj_fraction = 0;
					for( int j = 0; j < adj[idx]->n_data; j++ )
						if( adj[idx]->data[j].shift >= 0 )
						{
							jdx = adj[idx]->data[j].key;
							adj_fraction += model->population[jdx].app_user;
						}
					if( !set_empty(adj[idx]) )
						adj_fraction /= set_size(adj[idx]);

					double cutoff;
					if( real_fraction < fraction[age] )
						cutoff = real_fraction/4;
					else
						cutoff = 1 - (1-real_fraction)/4;
					//printf("adj_f = %lf, lo = %lf, hi = %lf, rf = %lf\n", adj_fraction, lo, hi, real_fraction);

					if( adj_fraction < min(cutoff, fraction[age]) && model->population[idx].app_user )
					{
						model->population[idx].app_user = FALSE;
						real_n_users[age]--;
						n_flips++;
					}
					else if( adj_fraction > max(cutoff, fraction[age]) && !model->population[idx].app_user )
					{
						model->population[idx].app_user = TRUE;
						real_n_users[age]++;
						n_flips++;
					}
				}
				if (DEBUG) {
					printf("n_flips = %d\n", n_flips);
				}
			}

			for( idx = 0; idx < model->params->n_total; idx++ )
				destroy_set(adj[idx]);
			free(adj);
		}
	}

	// TODO: error msg for not converging
	if (TRUE) {
		double cnt = 0;
		for (int i = 0; i<model->params->n_total; i++)
			cnt += model->population[i].app_user;
		printf("real app adoption = %lf\n", cnt/model->params->n_total);

		network *net;
		double freq[4];
		for (int i = 0; i<4; i++)
			freq[i] = 0;
		for( int j = 0; j <= model->n_occupation_networks; j++ )
		{
			if (j < model->n_occupation_networks)
				net = model->occupation_network[j];
			else
				net = model->household_network;

			for( long i = 0; i < net->n_edges; i++ )
			{
				long a = net->edges[i].id1;
				long b = net->edges[i].id2;
				short app_a = model->population[a].app_user;
				short app_b = model->population[b].app_user;
				freq[app_a + app_b]++;
				freq[3]++;
			}
		}
		for (int i = 0; i<3; i++)
			freq[i] /= freq[3];
		for (int i = 0; i<4; i++)
			printf("freq[%d] = %lf\n", i, freq[i]);
	}
	assert(FALSE);
}

/*****************************************************************************************
*  Name:		set_up_risk_scores
*  Description: Set up the risk scores used in calculation whether people are quarantined
******************************************************************************************/
void set_up_risk_scores( model *model )
{
	int idx, jdx, day;
	parameters *params = model->params;
	short max_days = params->days_of_interactions;

	params->risk_score = calloc( max_days, sizeof( double** ) );
	for( day = 0; day < max_days; day++ )
	{
		params->risk_score[ day] = calloc( N_AGE_GROUPS, sizeof( double* ) );
		for( idx = 0; idx < N_AGE_GROUPS; idx++ )
		{
			params->risk_score[ day ][ idx ] = calloc( N_AGE_GROUPS, sizeof( double ) );
			for( jdx = 0; jdx < N_AGE_GROUPS; jdx++ )
				params->risk_score[ day ][ idx ][ jdx ] = 1;
		}
	}

	params->risk_score_household = calloc( N_AGE_GROUPS, sizeof( double* ) );
	for( idx = 0; idx < N_AGE_GROUPS; idx++ )
	{
		params->risk_score_household[ idx ] = calloc( N_AGE_GROUPS, sizeof( double ) );
		for( jdx = 0; jdx < N_AGE_GROUPS; jdx++ )
			params->risk_score_household[ idx ][ jdx ] = 1;
	}
}

/*****************************************************************************************
*  Name:		destroy_risk_scores
*  Description: Destroy the risk scores used in calculation whether people are quarantined
******************************************************************************************/
void destroy_risk_scores( model *model )
{
	int idx, day;
	parameters *params = model->params;

	for( day = 0; day < params->days_of_interactions; day++ )
	{
		for( idx = 0; idx < N_AGE_GROUPS; idx++ )
			free( params->risk_score[ day ][ idx ] );
		free( params->risk_score[ day ] );
	}
	free( params->risk_score );

	for( idx = 0; idx < N_AGE_GROUPS; idx++ )
		free( params->risk_score_household[ idx ] );

	free( params->risk_score_household );
}

/*****************************************************************************************
*  Name:		set_up_trace_tokens
*  Description: sets up the stock trace_tokens note that these get recycled once we
*  				move to a later date
*  Returns:		void
******************************************************************************************/
void set_up_trace_tokens( model *model, float tokens_per_person )
{
	model->trace_token_block = NULL;
	model->next_trace_token = NULL;
	add_trace_tokens( model, tokens_per_person );
}

/*****************************************************************************************
*  Name:		add_trace_tokens
*  Description: adds additional trace_tokens note that these get recycled once we
*  				move to a later date
*  Returns:		void
******************************************************************************************/
void add_trace_tokens( model *model, float tokens_per_person )
{
	long n_tokens = ceil(  model->params->n_total * tokens_per_person );
	long idx;
	trace_token_block *block;
	block = calloc( 1, sizeof( trace_token_block ) );

	block->trace_tokens = calloc( n_tokens, sizeof( trace_token ) );
	block->next = model->trace_token_block;
	model->trace_token_block = block;

	block->trace_tokens[0].next_index = model->next_trace_token;
	for( idx = 1; idx < n_tokens; idx++ )
		block->trace_tokens[idx].next_index = &(block->trace_tokens[idx-1]);

	model->next_trace_token = &(block->trace_tokens[ n_tokens - 1 ]);
}

/*****************************************************************************************
*  Name:		new_trace_token
*  Description: gets a new trace token
*  Returns:		void
******************************************************************************************/
trace_token* create_trace_token( model *model, individual *indiv, int contact_time )
{
	trace_token *token = model->next_trace_token;

	model->next_trace_token = token->next_index;

	token->last = NULL;
	token->next = NULL;
	token->next_index = NULL;
	token->last_index = NULL;
	token->individual = indiv;
	token->traced_from = NULL;
	token->contact_time = contact_time;
	token->index_status = UNKNOWN;

	if( model->next_trace_token == NULL )
		add_trace_tokens( model, 0.25 );

	return token;
}

/*****************************************************************************************
*  Name:		index_trace_token
*  Description: get the index trace token at the start of a tracing cascade and
*  				assigns it to the indiviual, note if the individual already has
*  				one then we just add it to it
*  Returns:		void
******************************************************************************************/
trace_token* index_trace_token( model *model, individual *indiv )
{
	if( indiv->index_trace_token == NULL )
	{
		indiv->index_trace_token = create_trace_token( model, indiv, model->time );
		indiv->index_trace_token->contact_time = model->time;
	}

	return indiv->index_trace_token;
}

/*****************************************************************************************
*  Name:		remove_one_trace_token
*  Description: removes a single trace token from an individual
*  				returns it the stack of trace_tokens
*  Returns:		void
******************************************************************************************/
void remove_one_trace_token( model *model, trace_token *token )
{
	individual *indiv = token->individual;

	if( indiv->trace_tokens == token )
	{
		indiv->trace_tokens = token->next;
		if( indiv->trace_tokens != NULL )
			indiv->trace_tokens->last = NULL;
	}
	else
	{
		if( token->last != NULL )
			token->last->next = token->next;
		if( token->next != NULL )
			token->next->last = token->last;
	}

	if( token->next_index != NULL )
		token->next_index->last_index = token->last_index;
	if( token->last_index != NULL )
		token->last_index->next_index = token->next_index;

	// put the token back on the stack
	token->next_index = model->next_trace_token;
	model->next_trace_token = token;
	model->n_trace_tokens_used--;
}

/*****************************************************************************************
*  Name:		remove_traces_on_individual
*  Description: remove all the trace tokens an individual has received and
*  				remove from the the list the index keeps
*  Returns:		void
******************************************************************************************/
void remove_traces_on_individual( model *model, individual *indiv )
{
	trace_token *token, *last_index_token;
	trace_token *next_token = indiv->trace_tokens;
	individual *contact;

	while( next_token != NULL )
	{
		token = next_token;
		next_token = token->next;
		last_index_token = token->last_index;

		if( ( indiv->house_no == token->traced_from->house_no ) && token->traced_from->index_trace_token != NULL )
			continue;

		remove_one_trace_token( model, token );

		while( last_index_token->traced_from != NULL )
		{
			token      = last_index_token;
			last_index_token = token->last_index;

			if( token->traced_from->idx != indiv->idx )
				continue;

			contact = token->individual;
			remove_one_trace_token( model, token );

			if( (contact->trace_tokens == NULL) & (contact->index_trace_token == NULL) )
				intervention_quarantine_release( model, contact );
		}
	}
}

/*****************************************************************************************
*  Name:		remove_traced_on_this_trace
*  Description: add the end of a tracing event this removes the tag which
*  				prevents us from double counting people
*  Returns:		void
******************************************************************************************/
void remove_traced_on_this_trace( model *model, individual *indiv )
{
	trace_token *token = indiv->index_trace_token;
	trace_token *next_token;
	individual *contact;

	next_token = token->next_index;
	while( next_token != NULL )
	{
		token      = next_token;
		next_token = token->next_index;
		contact    = token->individual;

		if( contact->traced_on_this_trace < 1 )
			remove_one_trace_token( model, token );

		contact->traced_on_this_trace = FALSE;
	}
	indiv->traced_on_this_trace = FALSE;
}

/*****************************************************************************************
*  Name:		update_intervention_policy
*  Description: Updates the intervention policy by adjusting parmaters
******************************************************************************************/
void update_intervention_policy( model *model, int time )
{
	parameters *params = model->params;
	int type;

	if( time == 0 )
	{
		params->app_turned_on       = FALSE;
		params->lockdown_on	        = FALSE;
		params->lockdown_elderly_on	= FALSE;

		for( type = 0; type < N_INTERACTION_TYPES; type++ )
			params->relative_transmission_used[type] = params->relative_transmission[type];

		params->interventions_on = ( params->intervention_start_time == 0 );
	}

	if( time == params->intervention_start_time )
		params->interventions_on = TRUE;

	if( time == params->app_turn_on_time )
		set_model_param_app_turned_on( model, TRUE );

	if( time == params->manual_trace_time_on )
		set_model_param_manual_trace_on( model, TRUE );

	if( time == params->lockdown_time_on )
		set_model_param_lockdown_on( model, TRUE );

	if( time == params->lockdown_time_off )
		set_model_param_lockdown_on( model, FALSE );
	
	if( time == params->lockdown_elderly_time_on )
		set_model_param_lockdown_elderly_on( model, TRUE );

	if( time == params->lockdown_elderly_time_off )
		set_model_param_lockdown_elderly_on( model, FALSE );

	if( time == params->testing_symptoms_time_on )
		set_model_param_test_on_symptoms( model, TRUE );

	if( time == params->testing_symptoms_time_off )
		set_model_param_test_on_symptoms( model, FALSE );

	if( params->manual_trace_on )
	{
		model->manual_trace_interview_quota = params->manual_trace_n_workers * params->manual_trace_interviews_per_worker_day;
		model->manual_trace_notification_quota = params->manual_trace_n_workers * params->manual_trace_notifications_per_worker_day;
	}
}

/*****************************************************************************************
*  Name:		intervention_notify_contacts
*  Description: If the individual is an app user then we loop over the stored contacts
*  				and if they are app users too then count the interaction as traceable
*  Returns:		number of traceable interactions
******************************************************************************************/
int number_of_traceable_interactions(model *model, individual *indiv)
{
	if( !indiv->app_user || !model->params->app_turned_on )
		return ERROR;

	interaction *inter;
	individual *contact;
	parameters *params = model->params;
	int idx, ddx, day, n_contacts, traceable_inter = 0;

	// estimate the number of contacts
	n_contacts = 0;
	for( ddx = 0; ddx < params->days_of_interactions; ddx++ )
		n_contacts += indiv->n_interactions[ddx];
	long *contacts = calloc( n_contacts, sizeof( long ) );

	day = model->interaction_day_idx;
	for( ddx = 0; ddx < params->quarantine_days; ddx++ )
	{
		n_contacts = indiv->n_interactions[day];
		if( n_contacts > 0 )
		{
			inter = indiv->interactions[day];
			for( idx = 0; idx < n_contacts; idx++ )
			{
				contact = inter->individual;
				if( contact->app_user )
				{
					if( inter->traceable == UNKNOWN )
						inter->traceable = gsl_ran_bernoulli( rng, params->traceable_interaction_fraction );
					if( inter->traceable )
                        contacts[ traceable_inter++ ] = contact->idx;
				}
				inter = inter->next;
			}
		}
		ring_dec( day, model->params->days_of_interactions );
	}

	// now see how many unique contacts there are
	traceable_inter = n_unique_elements( contacts, traceable_inter );
	free( contacts );

    return traceable_inter;
}

/*****************************************************************************************
*  Name:		intervention_on_quarantine_until
*  Description: Quarantine an individual until a certain time
*  				If they are already in quarantine then extend quarantine until that time
*  Returns:		TRUE/FALSE - TRUE when quarantining for the first time on a trace
*  							 FALSE when not quarantining
******************************************************************************************/
int intervention_quarantine_until(
	model *model,
	individual *indiv,
	individual *trace_from,
	int time,
	int maxof,
	trace_token *index_token,
	int contact_time,
	double risk_score
)
{
	if (DEBUG) printf("i_q_u indiv %ld, t_o_t_t = %lf, risk_score = %lf\n", indiv->idx, indiv->traced_on_this_trace, risk_score);
	if( is_in_hospital( indiv ) )
		return FALSE;

	if( indiv->traced_on_this_trace >= 1 || risk_score == 0 )
		return FALSE;

	if (DEBUG) printf("A\n");
	if( index_token != NULL && indiv->traced_on_this_trace == 0 )
	{
		if (DEBUG) printf("B\n");
		// add the trace token to their list
		trace_token *token = create_trace_token( model, indiv, contact_time );
		token->index_status = index_token->index_status;
		token->traced_from  = trace_from;

		if( indiv->trace_tokens != NULL )
		{
			token->next = indiv->trace_tokens;
			indiv->trace_tokens->last = token;
		}
		indiv->trace_tokens = token;

		// then add it to the index_token list
		token->next_index = index_token->next_index;
		if( index_token->next_index != NULL )
			index_token->next_index->last_index = token;
		index_token->next_index = token;
		token->last_index = index_token;
	}
	indiv->traced_on_this_trace += risk_score;

	if (DEBUG) printf("C\n");
	if( indiv->traced_on_this_trace < 1 )
		return FALSE;

	if( time <= model->time )
		return FALSE;

	if (DEBUG) printf("D\n");
	if( indiv->quarantine_event == NULL )
	{
		if (DEBUG) printf("E\n");
		indiv->quarantine_event = add_individual_to_event_list( model, QUARANTINED, indiv, model->time, NULL );
		set_quarantine_status( indiv, model->params, model->time, TRUE, model );
	}

	if (DEBUG) printf("F\n");
	if( indiv->quarantine_release_event != NULL )
	{
		if (DEBUG) printf("G\n");
		if( maxof && indiv->quarantine_release_event->time > time )
			return TRUE;

		remove_event_from_event_list( model, indiv->quarantine_release_event );
	}

	if (DEBUG) printf("H\n");
	indiv->quarantine_release_event = add_individual_to_event_list( model, QUARANTINE_RELEASE, indiv, time, NULL );

	return TRUE;
}

/*****************************************************************************************
*  Name:		intervention_quarantine_release
*  Description: Release an individual held in quarantine
*  Returns:		void
******************************************************************************************/
void intervention_quarantine_release( model *model, individual *indiv )
{
	if( indiv->quarantine_release_event != NULL )
		remove_event_from_event_list( model, indiv->quarantine_release_event );

	if( indiv->quarantine_event != NULL )
	{
		remove_event_from_event_list( model, indiv->quarantine_event );
		set_quarantine_status( indiv, model->params, model->time, FALSE, model);
	};
}

/*****************************************************************************************
*  Name:		intervention_test_order
*  Description: Order a test for either today or a future date
*  Returns:		void
******************************************************************************************/
void intervention_test_order( model *model, individual *indiv, int time )
{
	if( indiv->quarantine_test_result == NO_TEST && !(indiv->infection_events->is_case) )
	{
        if( model->params->test_order_wait_priority == NO_PRIORITY_TEST )
        {
        	add_individual_to_event_list( model, TEST_TAKE, indiv, time, NULL );
        	indiv->quarantine_test_result = TEST_ORDERED;
        }
        else
        {
			int traceable_inter = number_of_traceable_interactions( model, indiv );

			if( traceable_inter >= model->params->priority_test_contacts[indiv->age_group] )
			{
				/* If individual had more than the n contacts, order a priority test */
				int test_order_wait_priority = model->params->test_order_wait_priority;
				add_individual_to_event_list( model, TEST_TAKE, indiv, model->time + test_order_wait_priority, NULL );
				indiv->quarantine_test_result = TEST_ORDERED_PRIORITY;
			} else
			{
				add_individual_to_event_list( model, TEST_TAKE, indiv, time, NULL );
				indiv->quarantine_test_result = TEST_ORDERED;
			}
        }
	}
}

/*****************************************************************************************
*  Name:		add_vaccine
*  Description: adds a new type of vaccine with specific properties
*
*  Arguments:   model             - pointer to the model
*  				full_efficacy     - probability the person does not contract the vaccine
*  				symptoms_efficacy - probability the person does not develop symptoms
*  				severe_efficacy   - probability the person does not develop severe symptoms
*  				time_to_protect   - delay before it takes effect
*  				vaccine_protection_period - length of time the vaccine provides protection
*
*  Returns:		the index of the vaccine
******************************************************************************************/
short add_vaccine(
	model *model,
	float *full_efficacy,
	float *symptoms_efficacy,
	float *severe_efficacy,
	short time_to_protect,
	short vaccine_protection_period
)
{
	vaccine *new_vaccine = calloc( 1, sizeof( vaccine ) );
	short n_strains      = model->params->max_n_strains;
	short is_full 		 = FALSE;
	short is_symptoms    = FALSE;
	short is_severe      = FALSE;

	new_vaccine->idx = 0;
	if( model->vaccines != NULL )
		new_vaccine->idx = model->vaccines->idx + 1;

	new_vaccine->full_efficacy     = calloc( n_strains, sizeof( float ) );
	new_vaccine->symptoms_efficacy = calloc( n_strains, sizeof( float ) );
	new_vaccine->severe_efficacy   = calloc( n_strains, sizeof( float ) );
	new_vaccine->time_to_protect   = time_to_protect;
	new_vaccine->vaccine_protection_period = vaccine_protection_period;

	for( int idx = 0; idx < n_strains; idx++ )
	{
		new_vaccine->full_efficacy[ idx ]     = full_efficacy[ idx ];
		new_vaccine->symptoms_efficacy[ idx ] = symptoms_efficacy[ idx ];
		new_vaccine->severe_efficacy[ idx ]   = severe_efficacy[ idx ];

		if( full_efficacy[ idx ] > 0 ) is_full = TRUE;
		if( symptoms_efficacy[ idx ] > 0 ) is_symptoms = TRUE;
		if( severe_efficacy[ idx ] > 0 ) is_severe = TRUE;
	}
	new_vaccine->is_full     = is_full;
	new_vaccine->is_symptoms = is_symptoms;
	new_vaccine->is_severe   = is_severe;
	new_vaccine->n_strains   = n_strains;

	new_vaccine->next = model->vaccines;
	model->vaccines   = new_vaccine;

	return new_vaccine->idx;
}

/*****************************************************************************************
*  Name:		get_vaccine_by_id
*  Description: returns a pointer to a vaccine a given ID
*  Returns:		pointer to vaccine
******************************************************************************************/
vaccine* get_vaccine_by_id( model *model, short vaccine_idx )
{
	vaccine *vaccine = model->vaccines;

	while( vaccine != NULL )
	{
		if( vaccine->idx == vaccine_idx )
			return vaccine;
		vaccine = vaccine->next;
	};

	return NULL;
}

/*****************************************************************************************
*  Name:		intervention_vaccinate
*  Description: Vaccinate an individual
*
*  Arguments:   model    - pointer to the model
*  				indiv    - pointer to the individual to be vaccinated
*  				vaccine  - pointer to the vaccine
*
*  Returns:		void
******************************************************************************************/
short intervention_vaccinate(
	model *model,
	individual *indiv,
	vaccine *vaccine
)
{
	if( indiv->vaccine_status != NO_VACCINE )
		return FALSE;

	if( ( indiv->status == DEATH ) | is_in_hospital( indiv ) )
		return FALSE;

	set_vaccine_status( indiv, model->params, ALL_STRAINS, VACCINE_NO_PROTECTION, model->time, vaccine->time_to_protect );
	add_individual_to_event_list( model, VACCINE_PROTECT, indiv, model->time + vaccine->time_to_protect, vaccine );

	return TRUE;
}

/*****************************************************************************************
*  Name:		intervention_vaccinate_by_idx
*  Description: Vaccinate an individual
*
*  Arguments:   model     - pointer to the model
*  				idx       - idx of person to vaccinate
*  				vaccine   - pointer to a vaccine
*
*  Returns:		1 if vaccinated 0 if not
******************************************************************************************/
short intervention_vaccinate_by_idx(
	model *model,
	long idx,
	vaccine *vaccine
)
{
	return intervention_vaccinate( model, &(model->population[idx]), vaccine );
}


/*****************************************************************************************
*  Name:		intervention_vaccinate_age_groups
*  Description: Vaccinate a fraction of each age group
*
*  Arguments:   model       - pointer to the model
*  				fractions   - the fraction of the population in each age group to vaccinate
*  				vaccine     - pointer to the type of vaccine
*
*  Returns:		long - the total number of people vaccinated
******************************************************************************************/
long intervention_vaccinate_age_group(
	model *model,
	double fractions[ N_AGE_GROUPS ],
	vaccine *vaccine,
	long n_vaccinated[ N_AGE_GROUPS ]
)
{
	long n_to_vaccinate[ N_AGE_GROUPS ];
	long total_to_vaccinate, total_vaccinated, pdx;
	short age;
	individual *indiv;

	total_to_vaccinate = 0;
	total_vaccinated  = 0;
	for( age = 0; age < N_AGE_GROUPS; age++ )
	{
		n_to_vaccinate[ age ] = round( model->n_population_by_age[ age ] * fractions[ age ] );
		n_vaccinated[ age ]   = 0;
		total_to_vaccinate   += n_to_vaccinate[ age ];
	}

	for( pdx = 0; pdx < model->params->n_total; pdx++ )
	{
		indiv = &(model->population[pdx]);
		age   = indiv->age_group;

		if( n_to_vaccinate[ age ] == n_vaccinated[ age ] )
			continue;
		if( indiv->vaccine_status != NO_VACCINE )
			continue;

		if( intervention_vaccinate( model, indiv, vaccine ) )
		{
			n_vaccinated[ age ]++;
			total_vaccinated++;
		}

		if( total_to_vaccinate == total_vaccinated )
			break;
	}

	return total_vaccinated;
}

/*****************************************************************************************
*  Name:		intervention_vaccine_protect
*  Description: The vaccine takes effect
*
*  Returns:		void
******************************************************************************************/
void intervention_vaccine_protect( model *model, individual *indiv, void* info )
{
	vaccine *vaccine = info;
	float r_unif = gsl_rng_uniform( rng );
	short n_strains = model->params->max_n_strains;
	short strain_idx;

	if( vaccine->is_full )
	{
		model->n_vaccinated_fully++;
		model->n_vaccinated_fully_by_age[ indiv->age_group ]++;
	}
	if( vaccine->is_symptoms )
	{
		model->n_vaccinated_symptoms++;
		model->n_vaccinated_symptoms_by_age[ indiv->age_group ]++;
	}

	short time_wane = model->time + vaccine->vaccine_protection_period - vaccine->time_to_protect;

	for( strain_idx = 0; strain_idx < n_strains; strain_idx++ )
	{
		if( vaccine->full_efficacy[ strain_idx ] > r_unif )
			set_vaccine_status( indiv, model->params, strain_idx, VACCINE_PROTECTED_FULLY, model->time, time_wane );

		if( vaccine->symptoms_efficacy[ strain_idx ] > r_unif )
			set_vaccine_status( indiv, model->params, strain_idx, VACCINE_PROTECTED_SYMPTOMS, model->time, time_wane );

		if( vaccine->severe_efficacy[ strain_idx ] > r_unif )
			set_vaccine_status( indiv, model->params, strain_idx, VACCINE_PROTECTED_SEVERE, model->time, time_wane );
	}
	add_individual_to_event_list( model, VACCINE_WANE, indiv, time_wane, vaccine );
}


/*****************************************************************************************
*  Name:		intervention_vaccine_wane
*  Description: The vaccine wanes (all types of protection)
*
*  Returns:		void
******************************************************************************************/
void intervention_vaccine_wane( model *model, individual *indiv, void* info )
{
	vaccine *vaccine = info;

	if( vaccine->is_full )
	{
		model->n_vaccinated_fully--;
		model->n_vaccinated_fully_by_age[ indiv->age_group ]--;
	}
	if( vaccine->is_symptoms )
	{
		model->n_vaccinated_symptoms++;
		model->n_vaccinated_symptoms_by_age[ indiv->age_group ]--;
	}

	set_vaccine_status( indiv, model->params, ALL_STRAINS, VACCINE_WANED, model->time, MAX_TIME );

}

/*****************************************************************************************
*  Name:		intervention_test_take
*  Description: An individual takes a test
*
*  				At the time of testing it will test positive if the individual has
*  				had the virus for less time than it takes for the test to be sensitive
*  Returns:		void
******************************************************************************************/
void intervention_test_take( model *model, individual *indiv )
{
	int result_time = model->time;
	if( indiv->quarantine_test_result == TEST_ORDERED_PRIORITY )
		result_time += model->params->test_result_wait_priority;
	else
		result_time += model->params->test_result_wait;

	int time_infected = time_infected( indiv );

	if( time_infected != UNKNOWN )
	{
		time_infected   = model->time - time_infected;
		int symptomatic = ( time_symptomatic( indiv ) <= model->time ) & ( time_symptomatic( indiv ) >= max( model->time - model->params->test_sensitive_period, 0 ) );

		if( ( symptomatic || time_infected >= model->params->test_insensitive_period ) && time_infected < model->params->test_sensitive_period )
			indiv->quarantine_test_result = gsl_ran_bernoulli( rng, model->params->test_sensitivity );
		else
			indiv->quarantine_test_result = gsl_ran_bernoulli( rng, 1 - model->params->test_specificity );
	}
	else
		indiv->quarantine_test_result = gsl_ran_bernoulli( rng, 1 - model->params->test_specificity );


	add_individual_to_event_list( model, TEST_RESULT, indiv, result_time, NULL );
}

/*****************************************************************************************
*  Name:		intervention_test_result
*  Description: An individual gets a test result
*
*  				 1. On a negative result remove the trace tokens from an individual
*  				 	and those with dependent trace tokens. Optionally release and
*  				 	then only if the person has no trace tokens after removal (we
*  				 	do not remove trace tokens if there is a remaining index case
*  				 	in the house, this is only done upon them having a negative test)
*  				 2. On a positive result they become a case and trigger the
*  				 	intervention_on_positive_result cascade
*  Returns:		void
******************************************************************************************/
void intervention_test_result( model *model, individual *indiv )
{
	if( indiv->quarantine_test_result == FALSE )
	{
		if( model->params->test_release_on_negative )
		{
			remove_traces_on_individual( model, indiv );
			intervention_trace_token_release( model, indiv );
		}
	}
	else
	{
		if( indiv->infection_events->is_case == FALSE )
		{
			set_case( indiv, model->time );
			add_individual_to_event_list( model, CASE, indiv, model->time, NULL );
		}

		if( !is_in_hospital( indiv ) || !(model->params->allow_clinical_diagnosis) )
			intervention_on_positive_result( model, indiv );
	}
	indiv->quarantine_test_result = NO_TEST;
}

/*****************************************************************************************
*  Name:		intervention_manual_trace
*  Description: Requests a trace today or on a future date.
*  Returns:		void
******************************************************************************************/
void intervention_manual_trace( model *model, individual *indiv )
{
	trace_token *index_token = index_trace_token( model, indiv );
	indiv->traced_on_this_trace = TRUE;

	//printf("manual tracing indiv %ld\n", indiv->idx);
	intervention_notify_contacts( model, indiv, 1, index_token, MANUAL_TRACE );

	remove_traced_on_this_trace( model, indiv );
}

/*****************************************************************************************
*  Name:		intervention_notify_contacts
*  Description: If the individual is an app user then we loop over the stored contacts
*  				and notifies them.
*  				Start from the oldest date, so that if we have met a contact
*  				multiple times we order the test from the first contact
*  Returns:		void
******************************************************************************************/
void intervention_notify_contacts(
	model *model,
	individual *indiv,
	int recursion_level,
	trace_token *index_token,
	int trace_type
)
{
	if( trace_type == DIGITAL_TRACE && (!indiv->app_user || !model->params->app_turned_on ))
		return;

	if( trace_type == MANUAL_TRACE )
	{
		if( !model->params->manual_trace_on )
			return;
		if( model->params->manual_trace_exclude_app_users && indiv->app_user && model->params->app_turned_on )
			return;
		if( model->manual_trace_interview_quota <= 0 )
			return;
		model->manual_trace_interview_quota--;
	}

	interaction *inter;
	individual *contact;
	parameters *params = model->params;
	int idx, ddx, day, n_contacts;
	double *risk_scores;

	day = model->interaction_day_idx;

	hashset *unique_idx = init_set(); // remove duplicate contacts

	for( ddx = 0; ddx < params->quarantine_days; ddx++ )
	{
		n_contacts  = indiv->n_interactions[day];
		risk_scores = params->risk_score[ ddx ][ indiv->age_group ];

		if( n_contacts > 0 )
		{
			inter = indiv->interactions[day];
			for( idx = 0; idx < n_contacts; idx++ )
			{
				contact = inter->individual;
				if( trace_type == DIGITAL_TRACE && contact->app_user )
				{
					if( inter->traceable == UNKNOWN )
						inter->traceable = gsl_ran_bernoulli( rng, params->traceable_interaction_fraction );
					if( inter->traceable == 1 )
						intervention_on_traced( model, contact, model->time - ddx, recursion_level, index_token, risk_scores[ contact->age_group ], trace_type );
				}
				else if( trace_type == MANUAL_TRACE )
				{
					if( inter->manual_traceable == UNKNOWN )
						inter->manual_traceable = gsl_ran_bernoulli( rng, params->manual_traceable_fraction[inter->type] );
					if( inter->manual_traceable && model->manual_trace_notification_quota > 0 )
					{
						model->manual_trace_notification_quota--;
						if ( params->novid_on )
						{
							if ( contact->app_user )
							{
								if ( !set_contains( unique_idx, contact->idx ) && gsl_ran_bernoulli( rng, params->novid_report_manual_traced ) )
									intervention_novid_alert( model, contact, 1 );
								set_insert( unique_idx, contact->idx );
							}
						}
						else
							intervention_on_traced( model, contact, model->time - ddx, recursion_level, index_token, risk_scores[ contact->age_group ], trace_type );
					}
				}
				inter = inter->next;
			}
		}
		ring_dec( day, model->params->days_of_interactions );
	}
}

/*****************************************************************************************
*  Name:		intervention_trace_token_release
*  Description: what to do when a trace_token is released
*  Returns:		void
******************************************************************************************/
void intervention_trace_token_release( model *model, individual *indiv )
{
	if (DEBUG) printf("t = %d:\ti_t_t_r indiv %ld\n", model->time, indiv->idx);
	individual *contact;
	trace_token *index_token = indiv->index_trace_token;
	trace_token *next_token, *token;
	//if (indiv->idx == 1994)
		//printf("Releasing trace token from indiv %ld, time = %d\n", indiv->idx, model->time);

	// remove the release token
	if( indiv->index_token_release_event != NULL )
	{
		remove_event_from_event_list( model, indiv->index_token_release_event );
		indiv->index_token_release_event = NULL;
	}

	if( index_token == NULL )
		return;

	indiv->index_trace_token = NULL;

	// if nobody traced then nothing to do
	next_token = index_token->next_index;

	while( next_token != NULL )
	{
		// get the next token on the list of this person trace_token
		token      = next_token;
		next_token = token->next_index;
		contact    = token->individual;
		remove_one_trace_token( model, token );


		if( (contact->trace_tokens == NULL) && (contact->index_trace_token == NULL) )
			intervention_quarantine_release( model, contact );
	}

	remove_one_trace_token( model, index_token );

	if( indiv->trace_tokens == NULL )
		intervention_quarantine_release( model, indiv );
	if (DEBUG) printf("end i_t_t_r\n");
}

/*****************************************************************************************
*  Name:		intervention_quarantine_household
*  Description: Quarantine everyone in a household
*  Returns:		void
******************************************************************************************/
void intervention_quarantine_household(
	model *model,
	individual *indiv,
	int time,
	int contact_trace,
	trace_token *index_token,
	int contact_time
)
{
	parameters *params = model->params;
	individual *contact;
	int idx, n, time_event, quarantine, time_test;
	long* members;
	double *risk_scores = model->params->risk_score_household[ indiv->age_group ];

	n          = model->household_directory->n_jdx[indiv->house_no];
	members    = model->household_directory->val[indiv->house_no];
	time_event = ifelse( time != UNKNOWN, time, model->time + sample_transition_time( model, TRACED_QUARANTINE_SYMPTOMS ) );

	for( idx = 0; idx < n; idx++ )
		if( members[idx] != indiv->idx )
		{
			contact = &(model->population[members[idx]]);

			if( contact->status == DEATH || is_in_hospital( contact ) || contact->infection_events->is_case )
				continue;

			quarantine = intervention_quarantine_until( model, contact, indiv, time_event, TRUE, index_token, contact_time, risk_scores[ contact->age_group ] );

			if( quarantine && params->test_on_traced && ( index_token->index_status == POSITIVE_TEST ) )
			{
				time_test = max( model->time + params->test_order_wait, contact_time + params->test_insensitive_period );
				intervention_test_order( model, contact, time_test );
			}

			if( contact_trace && ( params->quarantine_on_traced || params->test_on_traced ) )
				intervention_notify_contacts( model, contact, NOT_RECURSIVE, index_token, DIGITAL_TRACE );
		}
}

/*****************************************************************************************
*  Name:		intervention_quarantine_household_of_traced
*  Description: Quarantine household members of everyone on a trace list
*  Returns:		void
******************************************************************************************/
void intervention_quarantine_household_of_traced(
	model *model,
	trace_token *index_token
)
{
	individual *contact;
	int time_quarantine;
	trace_token *token = index_token;
	long house_no      = index_token->individual->house_no;

	while( token->next_index != NULL )
	{
		token   = token->next_index;
		contact = token->individual;
		contact->traced_on_this_trace = TRUE;
		token->index_status = index_token->index_status;

		if( ( contact->house_no != house_no ) & ( contact->quarantine_release_event != NULL ) )
		{
			time_quarantine = contact->quarantine_release_event->time;
			intervention_quarantine_household( model, contact, time_quarantine, FALSE, index_token, FALSE );
		}
	}
}

/*****************************************************************************************
*  Name:		intervention_index_case_symptoms_to_positive
*  Description: Intervention to take place when somebody who is already an index case
*  				and have previously sent out a message after reporting symptoms, receives
*  				a positive test result.
*
*				First of all - the first order contacts get an upgraded (red) message
*				which they will be much more likely to comply with
*
*  Returns:		void
******************************************************************************************/
void intervention_index_case_symptoms_to_positive(
	model *model,
	trace_token *index_token
)
{
	parameters *params = model->params;
	individual *contact;
	int time_quarantine, time_test;
	int trace_household = params->quarantine_household_on_traced_positive && !params->quarantine_household_on_traced_symptoms;
	trace_token *token  = index_token;
	long house_no       = index_token->individual->house_no;
	int contact_time;

	while( token->next_index != NULL )
	{
		token	= token->next_index;
		contact = token->individual;
		token->index_status = index_token->index_status;

 		if( contact->traced_on_this_trace == FALSE )
 		{
 			if( contact->status != DEATH && !is_in_hospital( contact ) && !contact->infection_events->is_case )
 			{
				contact_time = token->contact_time;
				if( gsl_ran_bernoulli( rng, params->quarantine_compliance_traced_positive  ) )
				{
					time_quarantine = contact_time + sample_transition_time( model, TRACED_QUARANTINE_POSITIVE );
					intervention_quarantine_until( model, contact, index_token->individual, time_quarantine, TRUE, NULL, contact_time, 1 );
				}

				if( ( contact->quarantine_release_event != NULL ) & ( params->test_on_traced == TRUE ) )
				{
					time_test = max( model->time + params->test_order_wait, contact_time + params->test_insensitive_period );
					intervention_test_order( model, contact, time_test );
				}

				if( trace_household & ( contact->house_no != house_no ) & ( contact->quarantine_release_event != NULL ) )
				{
					time_quarantine = contact->quarantine_release_event->time;

					intervention_quarantine_household( model, contact, time_quarantine, FALSE, index_token, FALSE );
				}
 			}
			contact->traced_on_this_trace = TRUE;
 		}
	}
}

/*****************************************************************************************
*  Name:		intervention_on_symptoms
*  Description: The interventions performed upon showing symptoms of a flu-like symptoms
*
*  				 1. If in quarantine already or drawn for self-quarantine then
*  				    quarantine for length of time symptomatic people do
*  				 2. If we have community testing on symptoms and a test has not been
*  				    ordered already then order one
*  				 3. Option to quarantine all household members upon symptoms
*  Returns:		void
******************************************************************************************/
void intervention_on_symptoms( model *model, individual *indiv )
{
	if( !model->params->interventions_on )
		return;

	/*
	// when daily_flu = 1, this blocks many people
	if( indiv->index_trace_token != NULL ) {
		printf("indiv %ld has trace token\n", indiv->idx);
		return;
	}
	*/
//	if (indiv->quarantined)
		//printf("i_o_s indiv already quarantined\n");

	int quarantine, time_event, test;
	parameters *params = model->params;

	double r_unif = gsl_rng_uniform( rng );
	// TODO: indivs who are already quarantined (through contact tracing) will react to symptoms early,
	// instead of reacting to their positive test later
	//quarantine = indiv->quarantined || ( r_unif < params->self_quarantine_fraction );
	quarantine = r_unif < params->self_quarantine_fraction;
	test = params->test_on_symptoms && ( r_unif < 1 ); // TODO: params->test_on_traced_fraction );
	if (DEBUG) printf("t = %d:\ti_o_s indiv %ld react = %d\n", model->time, indiv->idx, quarantine);

	trace_token *index_token;
	if( quarantine )
	{
		if (params->novid_on) {
			if ( indiv->app_user && gsl_ran_bernoulli( rng, params->novid_report_manual_traced ) )
				intervention_novid_alert( model, indiv, 0 );
			if( params->manual_trace_on &&
				( params->manual_trace_on_positive ||
				  ( params->manual_trace_on_hospitalization && is_in_hospital( indiv ) ) ) &&
				( params->quarantine_on_traced || params->test_on_traced )
			)
			{
				add_individual_to_event_list( model, MANUAL_CONTACT_TRACING, indiv, model->time + params->manual_trace_delay, NULL );
			}
			return;
		}
		else {
			index_token  = index_trace_token( model, indiv );
			index_token->index_status = SYMPTOMS_ONLY;

			time_event = model->time + sample_transition_time( model, SYMPTOMATIC_QUARANTINE );

			intervention_quarantine_until( model, indiv, NULL, time_event, TRUE, NULL, model->time, 1 );
			indiv->traced_on_this_trace = TRUE;

			if( params->quarantine_household_on_symptoms )
				intervention_quarantine_household( model, indiv, time_event, params->quarantine_household_contacts_on_symptoms, index_token, model->time );
			if( params->trace_on_symptoms && ( params->quarantine_on_traced || params->test_on_traced ) )
				intervention_notify_contacts( model, indiv, 1, index_token, DIGITAL_TRACE );

			remove_traced_on_this_trace( model, indiv );

			if( indiv->index_token_release_event != NULL )
				remove_event_from_event_list( model, indiv->index_token_release_event );
			indiv->index_token_release_event = add_individual_to_event_list( model, TRACE_TOKEN_RELEASE, indiv, model->time + max(params->quarantine_length_self, params->quarantine_length_traced_symptoms), NULL );
		}
	}
//	if( test )
//		intervention_test_order( model, indiv, model->time + params->test_order_wait );
}

/*****************************************************************************************
*  Name:		intervention_on_hospitalised
*  Description: The interventions performed upon becoming hopsitalised.
*
*  					1. Take a test immediately
*  					2. Option to use a clinical diagnosis to trigger contact tracing
*
*  Returns:		void
******************************************************************************************/
void intervention_on_hospitalised( model *model, individual *indiv )
{
	if( !model->params->interventions_on )
		return;

	if( indiv->quarantine_test_result == NO_TEST && !indiv->infection_events->is_case )
	{
		intervention_test_order( model, indiv, model->time );

		if( model->params->allow_clinical_diagnosis )
			intervention_on_positive_result( model, indiv );
	}
	else if( indiv->quarantine_test_result == TRUE &&
			 model->params->manual_trace_on &&
			 model->params->manual_trace_on_hospitalization &&
			 !model->params->manual_trace_on_positive )
	{
		add_individual_to_event_list( model, MANUAL_CONTACT_TRACING, indiv, model->time + model->params->manual_trace_delay, NULL );
	}
}

/*****************************************************************************************
*  Name:		intervention_on_positive_result
*  Description: The interventions performed upon receiving a positive test result
*
*  				 1. Patients who are not in hospital will be quarantined
*  				 2. Commence contact-tracing for patients not in hospital or hospital
*  				    patients if clinical diagnosis not being used as a trigger
*  Returns:		void
******************************************************************************************/
void intervention_on_positive_result( model *model, individual *indiv )
{
	if( !model->params->interventions_on )
		return;

	int time_event = UNKNOWN;
	parameters *params = model->params;
	int index_already  = ( indiv->index_trace_token != NULL );
	trace_token *index_token = index_trace_token( model, indiv );
	index_token->index_status = POSITIVE_TEST;
	int release_time = index_token->contact_time + params->quarantine_length_traced_positive;

	if( !is_in_hospital( indiv ) )
	{
		time_event = index_token->contact_time + sample_transition_time( model, TEST_RESULT_QUARANTINE );
		intervention_quarantine_until( model, indiv, NULL, time_event, TRUE, NULL, model->time, 1 );
	}
	indiv->traced_on_this_trace = TRUE;

	if( params->quarantine_household_on_positive )
		intervention_quarantine_household( model, indiv, time_event, params->quarantine_household_contacts_on_positive, index_token, model->time );

	if( params->trace_on_positive &&
	 ( !index_already || !params->trace_on_symptoms || params->retrace_on_positive ) &&
	  ( params->quarantine_on_traced || params->test_on_traced )
	)
		intervention_notify_contacts( model, indiv, 1, index_token, DIGITAL_TRACE );

	if ( params->novid_on && indiv->app_user )
		intervention_novid_alert( model, indiv, 0 );

	if( params->manual_trace_on &&
		( params->manual_trace_on_positive ||
		  ( params->manual_trace_on_hospitalization && is_in_hospital( indiv ) ) ) &&
	    ( params->quarantine_on_traced || params->test_on_traced )
	)
	{
		add_individual_to_event_list( model, MANUAL_CONTACT_TRACING, indiv, model->time + params->manual_trace_delay, NULL );
		release_time = max( release_time, model->time + params->manual_trace_delay);
	}

	if( index_already)
		intervention_index_case_symptoms_to_positive( model, index_token );

	if( indiv->index_token_release_event != NULL )
		remove_event_from_event_list( model, indiv->index_token_release_event );
	indiv->index_token_release_event = add_individual_to_event_list( model, TRACE_TOKEN_RELEASE, indiv, release_time, NULL );

	remove_traced_on_this_trace( model, indiv );
}

/******************************************************************************************
*  Name:		intervention_on_critical
*  Description: The interventions performed upon becoming critical
*  Returns:		void
******************************************************************************************/
void intervention_on_critical( model *model, individual *indiv )
{
	if( !model->params->interventions_on )
		return;
}

/*****************************************************************************************
*  Name:		intervention_on_traced
*  Description: Optional interventions performed upon becoming contact-traced
*
*   			1. Quarantine the individual
*   			2. Quarantine the individual and their household
*  				2. Order a test for the individual
*  				4. Recursive contact-trace
*
*  Arguments:	model 		 	- pointer to model
*  				indiv 		 	- pointer to person being traced
*  				contact_time 	- time at which the contact was made
*				recursion level - layers of the network to reach this connection
*				trace_type      - whether this trace was manual or digital
*
*  Returns:		void
******************************************************************************************/
void intervention_on_traced(
	model *model,
	individual *indiv,
	int contact_time,
	int recursion_level,
	trace_token *index_token,
	double risk_score,
	int trace_type
)
{
	if( is_in_hospital( indiv ) || indiv->infection_events->is_case )
		return;

	parameters *params = model->params;

	if( params->quarantine_on_traced )
	{
		int time_event = model->time;
		int quarantine;

		if( index_token->index_status == SYMPTOMS_ONLY )
		{
			if( gsl_ran_bernoulli( rng, params->quarantine_compliance_traced_symptoms ) )
				time_event = contact_time + sample_transition_time( model, TRACED_QUARANTINE_SYMPTOMS );

		}
		else if( index_token->index_status == POSITIVE_TEST )
		{
			if( gsl_ran_bernoulli( rng, params->quarantine_compliance_traced_positive ) )
				time_event = contact_time + sample_transition_time( model, TRACED_QUARANTINE_POSITIVE );
		}

		quarantine = intervention_quarantine_until( model, indiv, index_token->individual, time_event, TRUE, index_token, contact_time, risk_score );

		if( quarantine && params->test_on_traced && ( index_token->index_status == POSITIVE_TEST ) )
		{
			int time_test = max( model->time + params->test_order_wait, contact_time + params->test_insensitive_period );
			intervention_test_order( model, indiv, time_test );
		}

		if( quarantine && recursion_level != NOT_RECURSIVE )
		{
			if( ( params->quarantine_household_on_traced_positive && index_token->index_status == POSITIVE_TEST	) ||
				( params->quarantine_household_on_traced_symptoms && index_token->index_status == SYMPTOMS_ONLY ) )
				intervention_quarantine_household( model, indiv, time_event, FALSE, index_token, contact_time );
		}
	}

	if( recursion_level != NOT_RECURSIVE && recursion_level < params->tracing_network_depth )
		intervention_notify_contacts( model, indiv, recursion_level + 1, index_token, DIGITAL_TRACE );
}

/*****************************************************************************************
*  Name:		intervention_smart_release
*  Description: Release people from quarantine based upon how many people have developed
*				symptoms following being quarantined by the index case
*  Returns:		void
******************************************************************************************/
void intervention_smart_release( model *model )
{
	long idx, n_events;
	int day,n_symptoms, time_index;
	individual *indiv, *contact;
	event *event, *next_event;
	trace_token *token;

	int days =  model->params->quarantine_smart_release_day;

	day        = model->time + model->params->quarantine_length_traced_symptoms - days;
	time_index = model->time - days;

	if( time_index < 1)
		return;

	n_events    = model->event_lists[TRACE_TOKEN_RELEASE].n_daily_current[  day ];
	next_event  = model->event_lists[TRACE_TOKEN_RELEASE].events[day ];

	for( idx = 0; idx < n_events; idx++ )
	{
		event      = next_event;
		next_event = event->next;
		indiv      = event->individual;

		n_symptoms = 0;

		token = indiv->index_trace_token;
		if( token == NULL )
			continue;

		if( is_in_hospital( indiv ) || ( indiv->infection_events->times[CASE] >= time_index) )
			continue;

		token = token->next_index;
		while( token != NULL )
		{
			contact = token->individual;

      if( time_symptomatic( contact ) >= time_index )
			{
				n_symptoms++;
				break;
			}
			token = token->next_index;
		}

		if( n_symptoms == 0 )
		{
			intervention_trace_token_release( model, indiv );
		}
	}
}

/*****************************************************************************************
*  Name:		resolve_quarantine_reasons
*  Description: Determine a single recorded reason for an individual being quarantined;
				resolve multiple reasons for an individual being quarantined.  
				QUARANTINE_REASONS are listed in descending order of precedence.
*  Returns:		void
******************************************************************************************/

int resolve_quarantine_reasons(int *quarantine_reasons)
{
	int n_reasons = 0, reason, i;
	
	// Descending traverse (QUARANTINE_REASONS are listed in descending order of precedence)
	for(i = N_QUARANTINE_REASONS - 1; i >= 0; i--){
		
		if(quarantine_reasons[i] == TRUE){
			n_reasons++;
			reason = i;
		}
	}
	
	if((n_reasons > 0) && (n_reasons < N_QUARANTINE_REASONS))
		return reason;
	
	return UNKNOWN;
}

/*****************************************************************************************
*  Name:		intervention_novid_alert
*  Description: If the individual is a NOVID user and has a positive case at distance dist
*  				(either self positive, dist = 0 or contact-traced positive, dist = 1),
*  				alert other NOVID app users nearby
*  Returns:		void
******************************************************************************************/
void intervention_novid_alert(model *model, individual *indiv, int dist) {
	for (int d = 0; d + dist < MAX_NOVID_DIST; d++) {
		for (long i = 0; i < indiv->novid_n_adj[d]; i++) {
			long idx2 = indiv->novid_adj_list[d][i];
			individual *indiv2 = &(model->population[idx2]);
			// TODO: add compliance and dropout sampling
			indiv2->caution_until[d+dist] = model->time + model->params->novid_quarantine_length;
		}
	}
}

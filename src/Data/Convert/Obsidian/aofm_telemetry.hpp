#ifndef AOFM_TELEMETRY_H
#define AOFM_TELEMETRY_H

extern double time_track;
extern double time_simplify;
extern double time_normalize;
extern double time_latex_mark;
extern double time_doc_to_tree;
extern double time_group_markers;
extern double time_other;

extern double time_parse_latex_doc;
extern double time_latex_to_tree;
extern double aofm_math_time;
extern int aofm_math_count;

// Detailed breakdown of latex_to_tree
extern double time_l2t_kill_space;
extern double time_l2t_parsed_latex;
extern double time_l2t_finalize_doc;
extern double time_l2t_handle_matches;
extern double time_l2t_upgrade_tex;
extern double time_l2t_finalize_misc;
extern double time_l2t_drd_correct;
extern double time_l2t_style_check; // New: for the exists() call
extern double time_l2t_simplify_correct;
extern double time_l2t_latex_correct;
extern double time_l2t_guess_missing;
extern double time_l2t_post_metadata;

extern int count_l2t_is_document;
extern int count_l2t_total;

#endif

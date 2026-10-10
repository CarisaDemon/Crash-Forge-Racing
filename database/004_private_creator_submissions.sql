-- Creator Studio must not inherit the broad moderator queue visibility.
-- Applied in Supabase; this file preserves the migration for future deploys.
CREATE OR REPLACE VIEW public.forge_my_submissions
WITH (security_invoker = true, security_barrier = true)
AS
SELECT
    id, owner_id, title, category, mod_version, description,
    map_kind, racer_class, kart_drive, wheel_setup,
    status, moderator_note, created_at, replaces_submission_id,
    deleted_at, deleted_by, deletion_reason, zip_deleted_at,
    creator_dismissed_at
FROM public.mod_submissions
WHERE owner_id = (SELECT auth.uid())
  AND creator_dismissed_at IS NULL;

REVOKE ALL ON public.forge_my_submissions FROM PUBLIC, anon;
GRANT SELECT ON public.forge_my_submissions TO authenticated;

COMMENT ON VIEW public.forge_my_submissions IS
'Only the signed-in creator''s non-dismissed submissions; moderator review is a separate page.';

NOTIFY pgrst, 'reload schema';

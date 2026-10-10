-- Forge Hub deletion workflow. Applied to Supabase 2026-10-10.
-- Removing a ZIP preserves a minimal private tombstone because publication
-- audits and revision foreign keys must remain valid.
ALTER TABLE public.mod_submissions
 ADD COLUMN IF NOT EXISTS deleted_at timestamptz,
 ADD COLUMN IF NOT EXISTS deleted_by uuid REFERENCES auth.users(id),
 ADD COLUMN IF NOT EXISTS deletion_reason text,
 ADD COLUMN IF NOT EXISTS zip_deleted_at timestamptz,
 ADD COLUMN IF NOT EXISTS creator_dismissed_at timestamptz;

ALTER TABLE public.mod_submissions ADD CONSTRAINT forge_submission_deletion_reason_check
 CHECK ((deleted_at IS NULL AND deletion_reason IS NULL
             AND deleted_by IS NULL AND zip_deleted_at IS NULL
             AND creator_dismissed_at IS NULL)
        OR (deleted_at IS NOT NULL AND
           length(btrim(deletion_reason)) BETWEEN 8 AND 500));
CREATE INDEX IF NOT EXISTS forge_submission_deleted_idx
 ON public.mod_submissions (deleted_at)
 WHERE deleted_at IS NOT NULL;
ALTER POLICY forge_mod_edit ON public.mod_submissions
 USING ((select auth.uid())=owner_id AND status='pending' AND deleted_at IS NULL)
 WITH CHECK ((select auth.uid())=owner_id AND status='pending' AND deleted_at IS NULL);
ALTER POLICY forge_mod_read ON public.mod_submissions
 USING ((select auth.uid())=owner_id AND creator_dismissed_at IS NULL);

CREATE TABLE IF NOT EXISTS forge_internal.submission_deletion_events (
 id bigint GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
 submission_id uuid NOT NULL REFERENCES public.mod_submissions(id),
 action text NOT NULL CHECK (action IN ('marked','zip_removed','creator_dismissed')),
 actor_id uuid REFERENCES auth.users(id),
 reason text NOT NULL,
 occurred_at timestamptz NOT NULL DEFAULT now()
);
REVOKE ALL ON forge_internal.submission_deletion_events FROM PUBLIC,anon,authenticated;
REVOKE ALL ON SEQUENCE forge_internal.submission_deletion_events_id_seq
 FROM PUBLIC,anon,authenticated;

CREATE OR REPLACE FUNCTION public.forge_mark_submission_deleted(p_submission_id uuid, p_reason text, p_moderator_id uuid)
 RETURNS text
 LANGUAGE plpgsql
 SECURITY DEFINER
 SET search_path TO ''
AS $function$
DECLARE
 sub public.mod_submissions%rowtype;
 reason text := btrim(coalesce(p_reason,''));
BEGIN
 IF (select auth.uid()) IS NOT NULL THEN
  RAISE EXCEPTION 'Deletion must run through trusted publication service'
    USING ERRCODE='42501';
 END IF;
 IF length(reason)<8 OR length(reason)>500 THEN
  RAISE EXCEPTION 'Provide a deletion reason of 8-500 characters';
 END IF;
 IF NOT EXISTS (
   SELECT 1 FROM auth.identities i
   JOIN forge_internal.moderators m ON m.github_provider_id::text=i.provider_id
   WHERE i.user_id=p_moderator_id AND i.provider='github'
 ) THEN
  RAISE EXCEPTION 'Only a verified moderator can delete submissions'
    USING ERRCODE='42501';
 END IF;
 SELECT * INTO sub FROM public.mod_submissions
 WHERE id=p_submission_id FOR UPDATE;
 IF NOT FOUND THEN RAISE EXCEPTION 'Submission not found'; END IF;
 IF EXISTS (SELECT 1 FROM public.forge_public_mods
             WHERE submission_id=p_submission_id) THEN
  RAISE EXCEPTION 'Unpublish this mod before deleting it';
 END IF;
 IF EXISTS (SELECT 1 FROM public.mod_submissions
     WHERE replaces_submission_id=p_submission_id AND deleted_at IS NULL) THEN
  RAISE EXCEPTION 'Remove linked newer versions first';
 END IF;
 IF sub.deleted_at IS NULL THEN
  UPDATE public.mod_submissions
  SET deleted_at=now(),deleted_by=p_moderator_id,
      deletion_reason=reason,publication_blocked=true,
      creator_dismissed_at=NULL
  WHERE id=p_submission_id;
  INSERT INTO forge_internal.submission_deletion_events
   (submission_id,action,actor_id,reason)
   VALUES (p_submission_id,'marked',p_moderator_id,reason);
 END IF;
 RETURN sub.zip_path;
END;
$function$


CREATE OR REPLACE FUNCTION public.forge_complete_submission_deletion(p_submission_id uuid, p_moderator_id uuid)
 RETURNS boolean
 LANGUAGE plpgsql
 SECURITY DEFINER
 SET search_path TO ''
AS $function$
DECLARE path text;
BEGIN
 IF (select auth.uid()) IS NOT NULL THEN
  RAISE EXCEPTION 'ZIP removal confirmation must run through trusted publication service'
   USING ERRCODE='42501';
 END IF;
 IF NOT EXISTS (
   SELECT 1 FROM auth.identities i
   JOIN forge_internal.moderators m ON m.github_provider_id::text=i.provider_id
   WHERE i.user_id=p_moderator_id AND i.provider='github'
 ) THEN
  RAISE EXCEPTION 'Verified moderator required' USING ERRCODE='42501';
 END IF;
 SELECT zip_path INTO path FROM public.mod_submissions
 WHERE id=p_submission_id AND deleted_at IS NOT NULL FOR UPDATE;
 IF path IS NULL THEN RAISE EXCEPTION 'Deletion was not requested'; END IF;
 IF EXISTS (SELECT 1 FROM storage.objects
    WHERE bucket_id='forge-mod-queue' AND name=path) THEN
  RAISE EXCEPTION 'Private ZIP still exists: removal cannot be confirmed';
 END IF;
 UPDATE public.mod_submissions SET zip_deleted_at=coalesce(zip_deleted_at,now())
 WHERE id=p_submission_id;
 INSERT INTO forge_internal.submission_deletion_events
  (submission_id,action,actor_id,reason)
  VALUES (p_submission_id,'zip_removed',p_moderator_id,'Verified private ZIP is absent from Storage');
 RETURN TRUE;
END;
$function$


CREATE OR REPLACE FUNCTION public.forge_dismiss_deleted_submission(p_submission_id uuid)
 RETURNS boolean
 LANGUAGE plpgsql
 SECURITY DEFINER
 SET search_path TO ''
AS $function$
DECLARE uid uuid := (select auth.uid());
BEGIN
 IF uid IS NULL THEN
  RAISE EXCEPTION 'Sign in as the mod creator' USING ERRCODE='42501';
 END IF;
 UPDATE public.mod_submissions
 SET creator_dismissed_at=coalesce(creator_dismissed_at,now())
 WHERE id=p_submission_id AND owner_id=uid AND deleted_at IS NOT NULL
   AND zip_deleted_at IS NOT NULL;
 IF NOT FOUND THEN
  RAISE EXCEPTION 'Only this mod creator can dismiss a completed deletion'
    USING ERRCODE='42501';
 END IF;
 INSERT INTO forge_internal.submission_deletion_events
  (submission_id,action,actor_id,reason)
  VALUES (p_submission_id,'creator_dismissed',uid,'Creator hid deleted mod notification');
 RETURN TRUE;
END;
$function$


CREATE OR REPLACE FUNCTION public.forge_review_submission(p_submission_id uuid, p_decision text, p_note text DEFAULT ''::text)
 RETURNS TABLE(submission_id uuid, review_status text, review_note text, review_time timestamp with time zone)
 LANGUAGE plpgsql
 SECURITY DEFINER
 SET search_path TO ''
AS $function$
declare
  v_note text;
begin
 if (select auth.uid()) is null or not (select public.forge_is_moderator()) then
   raise exception 'Moderator access required' using errcode='42501';
 end if;
 if p_submission_id is null or p_decision not in ('approved','rejected','pending') then
   raise exception 'Invalid review action' using errcode='22023';
 end if;
 v_note:=btrim(coalesce(p_note,''));
 if char_length(v_note)>500 then
   raise exception 'Review note exceeds 500 characters' using errcode='22023';
 end if;
 if p_decision='rejected' and char_length(v_note)<8 then
   raise exception 'A rejection reason requires at least 8 characters' using errcode='22023';
 end if;

 return query
 update public.mod_submissions s
 set status=p_decision,
     moderator_note=v_note,
     reviewed_at=case when p_decision='pending' then null else now() end,
     reviewed_by=case when p_decision='pending' then null else (select auth.uid()) end
 where s.id=p_submission_id AND s.deleted_at IS NULL
   and (
     (s.status='pending' and p_decision in ('approved','rejected'))
     or (s.status='approved' and p_decision in ('pending','rejected'))
   )
   and not exists (
     select 1 from public.forge_public_mods fm
     where fm.submission_id=s.id
   )
 returning s.id,s.status,s.moderator_note,s.reviewed_at;
 if not found then
   raise exception 'Request is not reviewable, or it is still published: unpublish first'
     using errcode='P0002';
 end if;
end $function$


CREATE OR REPLACE FUNCTION public.forge_set_publication_hold(p_submission_id uuid, p_hold boolean, p_note text DEFAULT ''::text)
 RETURNS TABLE(submission_id uuid, publication_blocked boolean, review_status text)
 LANGUAGE plpgsql
 SECURITY DEFINER
 SET search_path TO ''
AS $function$
declare v_note text;
begin
  if (select auth.uid()) is null
      or not (select public.forge_is_moderator()) then
    raise exception 'Moderator permission required' using errcode='42501';
  end if;
  if p_submission_id is null or p_hold is null then
    raise exception 'Invalid publication hold request' using errcode='22023';
  end if;
  v_note:=btrim(coalesce(p_note,''));
  if char_length(v_note)<8 or char_length(v_note)>500 then
    raise exception 'Provide an administrative note between 8 and 500 characters'
      using errcode='22023';
  end if;

  return query
  update public.mod_submissions s
    set publication_blocked=p_hold
  where s.id=p_submission_id AND s.deleted_at IS NULL
    and s.status='approved'
    and s.publication_blocked<>p_hold
    and not exists (
      select 1 from public.forge_public_mods fm
      where fm.submission_id=s.id
    )
  returning s.id,s.publication_blocked,s.status;
  if not found then
    raise exception 'Already in requested state, not approved, or still published'
      using errcode='P0002';
  end if;
  insert into forge_internal.publication_hold_events
   (submission_id,moderator_id,on_hold,note)
  values(p_submission_id,(select auth.uid()),p_hold,v_note);
end $function$


CREATE OR REPLACE FUNCTION public.forge_finalize_publication(p_submission_id uuid, p_license text, p_rights_basis text, p_moderator_id uuid)
 RETURNS text
 LANGUAGE plpgsql
 SECURITY DEFINER
 SET search_path TO ''
AS $function$
declare
  sub public.mod_submissions%rowtype;
  creator public.creator_profiles%rowtype;
  prior_release public.forge_public_mods%rowtype;
  pub_id text;
  pub_path text;
  blob_url text;
  profile_url text;
  basis text;
begin
  if (select auth.uid()) is not null then
    raise exception 'Publication must run through the trusted server' using errcode='42501';
  end if;
  select * into sub from public.mod_submissions
  where id=p_submission_id and status='approved' and deleted_at IS NULL and not publication_blocked
  for update;
  if not found then raise exception 'Submission is not approved or has a safety hold'; end if;
  if p_license not in
    ('All rights reserved','CC-BY-4.0','CC0-1.0','Game rip - source credited',
     'Community upload - rights unverified') then
    raise exception 'Unsupported publication label';
  end if;
  basis:=btrim(coalesce(p_rights_basis,''));
  if length(basis)>1000 then
    raise exception 'Rights evidence is optional; maximum 1000 characters';
  end if;
  if p_license='Game rip - source credited'
    and not (sub.description ~* '(ripped|extracted|ported|taken)[[:space:]]+from[[:space:]]+[[:alnum:]]') then
    raise exception 'Please identify the original game/source in the mod description';
  end if;
  if not exists (
    select 1 from auth.identities i
    join forge_internal.moderators mod
      on mod.github_provider_id::text=i.provider_id
    where i.user_id=p_moderator_id and i.provider='github'
  ) then
    raise exception 'Only the verified moderator can publish' using errcode='42501';
  end if;
  select * into creator from public.creator_profiles where user_id=sub.owner_id;
  if not found then raise exception 'Creator profile missing'; end if;
  /* An approved revision inherits the existing public identity.
     The prior ZIP remains public until this transaction succeeds. */
  if sub.replaces_submission_id is not null then
    select * into prior_release
    from public.forge_public_mods
    where submission_id=sub.replaces_submission_id for update;
    if prior_release.id is not null and
       (prior_release.author_github_id<>creator.github_id or
        prior_release.type<>sub.category or
        lower(prior_release.title)<>lower(sub.title)) then
      raise exception 'Version target owner/category/title mismatch';
    end if;
  end if;
  if prior_release.id is null and exists (
    select 1 from public.forge_public_mods m
    where m.author_github_id=creator.github_id
      and m.type=sub.category and lower(m.title)=lower(sub.title)
  ) then
    raise exception 'This mod already exists publicly: submit and approve a revision';
  end if;
  pub_id:=coalesce(prior_release.id,'forge-'||replace(sub.id::text,'-',''));
  pub_path:='mods/'||sub.id::text||'.zip';
  if not exists (
    select 1 from storage.objects
    where bucket_id='forge-public-mods' and name=pub_path
  ) then
    raise exception 'The public ZIP has not been copied to release storage';
  end if;
  blob_url:='https://mjvpkerobjgoldmimyxz.supabase.co/storage/v1/object/public/forge-public-mods/'||pub_path;
  profile_url:='https://carisademon.github.io/Crash-Forge-Racing/forge-hub/creator.html?user='||creator.github_login;
  if prior_release.id is not null then
    update public.forge_public_mods m
    set submission_id=sub.id,title=sub.title,
        author=creator.display_name,author_github_login=creator.github_login,
        author_github_id=creator.github_id,
        type=sub.category,version=sub.mod_version,description=sub.description,
        map_kind=sub.map_kind,racer_class=sub.racer_class,
        kart_drive=sub.kart_drive,wheel_setup=sub.wheel_setup,
        download_url=blob_url,page_url=profile_url,
        sha256=sub.sha256,license=p_license,published_at=now()
    where m.id=prior_release.id
      and m.submission_id=prior_release.submission_id;
    if not found then
      raise exception 'The original public mod changed while publishing this update';
    end if;
    update forge_internal.publication_audits a
      set removed_at=now(),removed_by=p_moderator_id,
          removal_note='Superseded by approved revision '||sub.id::text
      where a.submission_id=prior_release.submission_id
        and a.removed_at is null;
  else
    insert into public.forge_public_mods
      (id,submission_id,title,author,author_github_login,author_github_id,
      type,version,description,map_kind,racer_class,kart_drive,wheel_setup,
      download_url,page_url,sha256,license)
    values
      (pub_id,sub.id,sub.title,creator.display_name,creator.github_login,creator.github_id,
      sub.category,sub.mod_version,sub.description,sub.map_kind,sub.racer_class,
      sub.kart_drive,sub.wheel_setup,blob_url,profile_url,sub.sha256,p_license);
  end if;
  insert into forge_internal.publication_audits
    (submission_id,published_mod_id,moderator_id,rights_basis,license)
  values
    (sub.id,pub_id,p_moderator_id,basis,p_license)
  on conflict (submission_id) do update
    set moderator_id=excluded.moderator_id,
        published_mod_id=excluded.published_mod_id,
        rights_basis=excluded.rights_basis,
        license=excluded.license,
        published_at=now(),
        removed_at=null,
        removed_by=null,
        removal_note=null;
  return pub_id;
end $function$


CREATE OR REPLACE FUNCTION public.forge_validate_mod_revision()
 RETURNS trigger
 LANGUAGE plpgsql
 SECURITY DEFINER
 SET search_path TO ''
AS $function$
DECLARE previous public.mod_submissions%rowtype;
BEGIN
 IF NEW.replaces_submission_id IS NULL THEN RETURN NEW; END IF;
 IF NEW.replaces_submission_id=NEW.id THEN
   RAISE EXCEPTION 'A mod cannot replace itself' USING ERRCODE='23514';
 END IF;
 SELECT * INTO previous FROM public.mod_submissions
 WHERE id=NEW.replaces_submission_id;
 IF NOT FOUND OR previous.status<>'approved' OR previous.deleted_at IS NOT NULL
    OR previous.owner_id<>NEW.owner_id
    OR previous.category<>NEW.category
    OR lower(previous.title)<>lower(NEW.title) THEN
   RAISE EXCEPTION 'New version must reference an approved mod owned by this creator, with the same name/category'
     USING ERRCODE='23514';
 END IF;
 IF lower(btrim(previous.mod_version))=lower(btrim(NEW.mod_version)) THEN
   RAISE EXCEPTION 'A new version must use a different version label'
     USING ERRCODE='23514';
 END IF;
 RETURN NEW;
END;
$function$


REVOKE EXECUTE ON FUNCTION public.forge_mark_submission_deleted(uuid,text,uuid)
 FROM PUBLIC,anon,authenticated;
REVOKE EXECUTE ON FUNCTION public.forge_complete_submission_deletion(uuid,uuid)
 FROM PUBLIC,anon,authenticated;
GRANT EXECUTE ON FUNCTION public.forge_mark_submission_deleted(uuid,text,uuid)
 TO service_role;
GRANT EXECUTE ON FUNCTION public.forge_complete_submission_deletion(uuid,uuid)
 TO service_role;
REVOKE EXECUTE ON FUNCTION public.forge_dismiss_deleted_submission(uuid)
 FROM PUBLIC,anon;
GRANT EXECUTE ON FUNCTION public.forge_dismiss_deleted_submission(uuid)
 TO authenticated;

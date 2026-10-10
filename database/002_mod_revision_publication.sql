-- Published mod revisions: do not replace public ZIPs until approval.
-- Applied live on 2026-10-10; documented here for future deployment.
ALTER TABLE public.mod_submissions
 ADD COLUMN IF NOT EXISTS replaces_submission_id uuid
 REFERENCES public.mod_submissions(id) ON DELETE RESTRICT;
ALTER TABLE public.mod_submissions
 ADD CONSTRAINT forge_revision_not_self
 CHECK (replaces_submission_id IS NULL OR replaces_submission_id<>id);
CREATE UNIQUE INDEX forge_one_open_revision_per_prior
 ON public.mod_submissions(replaces_submission_id)
 WHERE replaces_submission_id IS NOT NULL AND status IN ('pending','approved');

CREATE OR REPLACE FUNCTION public.forge_validate_mod_revision()
RETURNS trigger LANGUAGE plpgsql SECURITY DEFINER SET search_path TO ''
AS $revision$
DECLARE previous public.mod_submissions%rowtype;
BEGIN
 IF NEW.replaces_submission_id IS NULL THEN RETURN NEW; END IF;
 IF NEW.replaces_submission_id=NEW.id THEN
   RAISE EXCEPTION 'A mod cannot replace itself' USING ERRCODE='23514';
 END IF;
 SELECT * INTO previous FROM public.mod_submissions WHERE id=NEW.replaces_submission_id;
 IF NOT FOUND OR previous.status<>'approved'
    OR previous.owner_id<>NEW.owner_id OR previous.category<>NEW.category
    OR lower(previous.title)<>lower(NEW.title) THEN
   RAISE EXCEPTION 'New version must reference an approved mod owned by this creator, with the same name/category'
     USING ERRCODE='23514';
 END IF;
 IF lower(btrim(previous.mod_version))=lower(btrim(NEW.mod_version)) THEN
   RAISE EXCEPTION 'A new version must use a different version label' USING ERRCODE='23514';
 END IF;
 RETURN NEW;
END;
$revision$;
CREATE TRIGGER forge_validate_mod_revision_before_write
 BEFORE INSERT OR UPDATE OF replaces_submission_id,owner_id,category,title,mod_version
 ON public.mod_submissions FOR EACH ROW
 EXECUTE FUNCTION public.forge_validate_mod_revision();

ALTER TABLE public.forge_public_mods DROP CONSTRAINT forge_public_mods_license_check;
ALTER TABLE public.forge_public_mods ADD CONSTRAINT forge_public_mods_license_check
 CHECK (license IN ('All rights reserved','CC-BY-4.0','CC0-1.0',
                   'Game rip - source credited','Community upload - rights unverified'));

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
  where id=p_submission_id and status='approved' and not publication_blocked
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


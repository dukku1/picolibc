/* uinfo.cc: user info (uid, gid, etc...)

   Copyright 1996, 1997, 1998, 1999, 2000, 2001, 2002 Red Hat, Inc.

This file is part of Cygwin.

This software is a copyrighted work licensed under the terms of the
Cygwin license.  Please consult the file "CYGWIN_LICENSE" for
details. */

#include "winsup.h"
#include <pwd.h>
#include <unistd.h>
#include <winnls.h>
#include <wininet.h>
#include <utmp.h>
#include <limits.h>
#include <stdlib.h>
#include <lm.h>
#include <errno.h>
#include <sys/cygwin.h>
#include "pinfo.h"
#include "security.h"
#include "fhandler.h"
#include "path.h"
#include "dtable.h"
#include "cygerrno.h"
#include "cygheap.h"
#include "registry.h"
#include "child_info.h"

void
internal_getlogin (cygheap_user &user)
{
  struct passwd *pw = NULL;

  if (wincap.has_security ())
    {
      HANDLE ptok = INVALID_HANDLE_VALUE;
      DWORD siz;
      cygsid tu;
      DWORD ret = 0;

      /* Try to get the SID either from current process and
	 store it in user.psid */
      if (!OpenProcessToken (GetCurrentProcess (),
			     TOKEN_ADJUST_DEFAULT | TOKEN_QUERY,
			     &ptok))
	system_printf ("OpenProcessToken(): %E\n");
      else if (!GetTokenInformation (ptok, TokenUser, &tu, sizeof tu, &siz))
	system_printf ("GetTokenInformation(): %E");
      else if (!(ret = user.set_sid (tu)))
        system_printf ("Couldn't retrieve SID from access token!");
       /* We must set the user name, uid and gid.
	 If we have a SID, try to get the corresponding Cygwin
	 password entry. Set user name which can be different
	 from the Windows user name */
       if (ret)
	 {
	  cygsid gsid (NO_SID);
	  cygsid psid;

	  for (int pidx = 0; (pw = internal_getpwent (pidx)); ++pidx)
	    if (psid.getfrompw (pw) && EqualSid (user.sid (), psid))
	      {
		user.set_name (pw->pw_name);
		struct __group32 *gr = getgrgid32 (pw->pw_gid);
		if (gr)
		  if (!gsid.getfromgr (gr))
		      gsid = NO_SID;
		break;
	      }

	  /* Set token owner to the same value as token user and
	     primary group to the group in /etc/passwd. */
	  if (!SetTokenInformation (ptok, TokenOwner, &tu, sizeof tu))
	    debug_printf ("SetTokenInformation(TokenOwner): %E");
	  if (gsid && !SetTokenInformation (ptok, TokenPrimaryGroup,
					    &gsid, sizeof gsid))
	    debug_printf ("SetTokenInformation(TokenPrimaryGroup): %E");
	 }

      if (ptok != INVALID_HANDLE_VALUE)
	CloseHandle (ptok);
    }

  if (!pw)
    pw = getpwnam (user.name ());

  if (pw)
    {
      user.real_uid = pw->pw_uid;
      user.real_gid = pw->pw_gid;
    }
  else
    {
      user.real_uid = DEFAULT_UID;
      user.real_gid = DEFAULT_GID;
    }

  (void) cygheap->user.ontherange (CH_HOME, pw);

  return;
}

void
uinfo_init ()
{
  if (!child_proc_info)
    internal_getlogin (cygheap->user); /* Set the cygheap->user. */

  /* Real and effective uid/gid are identical on process start up. */
  myself->uid = cygheap->user.orig_uid = cygheap->user.real_uid;
  myself->gid = cygheap->user.orig_gid = cygheap->user.real_gid;
  cygheap->user.set_orig_sid();      /* Update the original sid */

  cygheap->user.token = INVALID_HANDLE_VALUE; /* No token present */
}

extern "C" char *
getlogin (void)
{
#ifdef _MT_SAFE
  char *this_username=_reent_winsup ()->_username;
#else
  static char this_username[UNLEN + 1] NO_COPY;
#endif

  return strcpy (this_username, cygheap->user.name ());
}

extern "C" __uid32_t
getuid32 (void)
{
  return cygheap->user.real_uid;
}

extern "C" __uid16_t
getuid (void)
{
  return cygheap->user.real_uid;
}

extern "C" __gid32_t
getgid32 (void)
{
  return cygheap->user.real_gid;
}

extern "C" __gid16_t
getgid (void)
{
  return cygheap->user.real_gid;
}

extern "C" __uid32_t
geteuid32 (void)
{
  return myself->uid;
}

extern "C" __uid16_t
geteuid (void)
{
  return myself->uid;
}

extern "C" __gid32_t
getegid32 (void)
{
  return myself->gid;
}

extern "C" __gid16_t
getegid (void)
{
  return myself->gid;
}

/* Not quite right - cuserid can change, getlogin can't */
extern "C" char *
cuserid (char *src)
{
  if (!src)
    return getlogin ();

  strcpy (src, getlogin ());
  return src;
}

const char *
cygheap_user::ontherange (homebodies what, struct passwd *pw)
{
  static char buf[MAX_PATH + 1];
  static char homedrive_buf[3];
  LPUSER_INFO_3 ui = NULL;
  WCHAR wuser[UNLEN + 1];
  NET_API_STATUS ret;

  if (what == CH_HOME)
    {
      char *p;
      if ((p = getenv ("HOMEDRIVE")))
	{
	  memcpy (homedrive_buf, p, 2);
	  homedrive = homedrive_buf;
	}
      if ((p = getenv ("HOMEPATH")))
	{
	  strcpy (buf, p);
	  homepath = buf;
	}
      if ((p = getenv ("HOME")))
	debug_printf ("HOME is already in the environment %s", p);
      else
	{
	  if (!pw)
	    pw = getpwnam (name ());
	  if (pw && pw->pw_dir && *pw->pw_dir)
	    {
	      setenv ("HOME", pw->pw_dir, 1);
	      debug_printf ("Set HOME (from /etc/passwd) to %s", pw->pw_dir);
	    }
	  else if (homedrive && homepath)
	    {
	      char home[MAX_PATH];
	      strcpy (buf, homedrive);
	      strcat (buf, homepath);
	      cygwin_conv_to_full_posix_path (buf, home);
	      setenv ("HOME", home, 1);
	      debug_printf ("Set HOME (from HOMEDRIVE/HOMEPATH) to %s", home);
	    }
	}
    }

  if (homedrive == NULL || !homedrive[0])
    {
      if (!pw)
	pw = getpwnam (name ());
      if (pw && pw->pw_dir && *pw->pw_dir)
	cygwin_conv_to_full_win32_path (pw->pw_dir, buf);
      else
	{
	  sys_mbstowcs (wuser, name (), sizeof (wuser) / sizeof (*wuser));
	  if ((ret = NetUserGetInfo (NULL, wuser, 3, (LPBYTE *)&ui)))
	    {
	      if (env_logsrv ())
		{
		  WCHAR wlogsrv[INTERNET_MAX_HOST_NAME_LENGTH + 3];
		  strcpy (buf, env_logsrv ());
		  sys_mbstowcs (wlogsrv, buf, sizeof (wlogsrv) / sizeof(*wlogsrv));
		  ret = NetUserGetInfo (wlogsrv, wuser, 3,(LPBYTE *)&ui);
		}
	    }
	  if (!ret)
	    {
	      char *p;
	      sys_wcstombs (buf, ui->usri3_home_dir, MAX_PATH);
	      if (!buf[0])
		{
		  sys_wcstombs (buf, ui->usri3_home_dir_drive, MAX_PATH);
		  if (buf[0])
		    strcat (buf, "\\");
		  else if (!GetSystemDirectory (buf, MAX_PATH))
		    strcpy (buf, "c:\\");
		  else if ((p = strchr (buf, '\\')))
		    p[1] = '\0';
		}
	    }
	  if (ui)
	    NetApiBufferFree (ui);
	}

      if (buf[1] != ':')
	{
	  homedrive_buf[0] = homedrive_buf[1] = '\0';
	  homepath = buf;
	}
      else
	{
	  homedrive_buf[0] = buf[0];
	  homedrive_buf[1] = buf[1];
	  homepath = buf + 2;
	}
      homedrive = homedrive_buf;
    }

  switch (what)
    {
    case CH_HOMEDRIVE:
      return homedrive;
    case CH_HOMEPATH:
      return homepath;
    default:
      return homepath;
    }
}

const char *
cygheap_user::env_logsrv ()
{
  if (plogsrv)
    return plogsrv;

  char logsrv[INTERNET_MAX_HOST_NAME_LENGTH + 3];
  if (!get_logon_server (env_domain (), logsrv, NULL))
    return NULL;
  return plogsrv = cstrdup (logsrv);
}

const char *
cygheap_user::env_domain ()
{
  if (pdomain)
    return pdomain;

  char username[UNLEN + 1];
  DWORD ulen = sizeof (username);
  char userdomain[DNLEN + 1];
  DWORD dlen = sizeof (userdomain);
  SID_NAME_USE use;

  if (!LookupAccountSid (NULL, sid (), username, &ulen,
			 userdomain, &dlen, &use))
    {
      __seterrno ();
      return NULL;
    }
  return pdomain = cstrdup (userdomain);
}

const char *
cygheap_user::env_userprofile ()
{
  static char buf[512]; /* FIXME: This shouldn't be static. */
  if (strcasematch (name (), "SYSTEM") || !env_domain () || !env_logsrv ())
    return NULL;

  if (get_registry_hive_path (sid (), buf))
    return buf;
  else
    return NULL;
}

const char *
cygheap_user::env_homepath ()
{
  return ontherange (CH_HOMEPATH);
}

const char *
cygheap_user::env_homedrive ()
{
  return ontherange (CH_HOMEDRIVE);
}

const char *
cygheap_user::env_name ()
{
  return name ();
}

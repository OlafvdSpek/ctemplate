# ctemplate #

CTemplate is a simple but powerful template language for C++. It emphasizes separating logic from presentation: it is impossible to embed application logic in this template language.

Here's an example of how to use it: not complete, but gives a good feel for what the ctemplate API looks like.

Here is a simple template file:
```
Hello {{NAME}},
You have just won ${{VALUE}}!
{{#IN_CA}}Well, ${{TAXED_VALUE}}, after taxes.{{/IN_CA}}
```

Here is a C++ program to fill in the template, which we assume is stored in the file 'example.tpl':
```
#include <cstdlib>
#include <iostream>  
#include <string>
#include <ctemplate/template.h>  

int main() {
  ctemplate::TemplateDictionary dict("example");
  int winnings = rand() % 100000;
  dict["NAME"] = "John Smith";
  dict["VALUE"] = winnings;
  dict.SetFormattedValue("TAXED_VALUE", "%.2f", winnings * 0.83);
  // For now, assume everyone lives in CA.
  // (Try running the program with a 0 here instead!)
  if (1) {
    dict.ShowSection("IN_CA");
  }
  std::string output;
  ctemplate::ExpandTemplate("example.tpl", ctemplate::DO_NOT_STRIP, &dict, &output);
  std::cout << output;
  return 0;
}
```

If you are interested in this templating language but are programming in Java, consider [Hapax](http://xfltr.com/templates/), which is similar to ctemplate.  There is also a pure C template system called [ngTemplate](https://github.com/breckinloggins/ngtemplate) that is intended to be compatible with ctemplate.

## 19 March 2014 ##

Ctemplate 2.3 has been released. Fixed some more C++11 issues.
Removed deprecated Template::SetEscapedValueAndShowSection() and Template::ReloadIfChanged().

  * https://xwis.net/ctemplate/ctemplate-2.3.tar.gz
  * https://xwis.net/ctemplate/ctemplate-2.3.zip

## 18 April 2012 ##

Ctemplate 2.2 has been released. Several issues reported by G++ 4.7 have been fixed.

## 22 March 2012 ##

2.1 has been released. Only source packages are provided, you can easily build binary packages yourself if desired.

`operator[]` has been added to `TemplateDictionary`.


## 10 February 2012 - Request for Requests ##

Hi, I'm Olaf and I'm your new ctemplate maintainer. Ctemplate is in great shape, thanks to the work of Craig Silvers. I'd like to know how ctemplate could be improved. What updates would **you** like to see?

Please post them at http://groups.google.com/group/google-ctemplate


## 24 January 2012 ##

I've just released ctemplate 2.0.  It has no functional changes from
ctemplate 1.2.

The `google-ctemplate` project has been renamed to `ctemplate`.  I
(csilvers) am stepping down as maintainer, to be replaced by Olaf van
der Spek.  Welcome to the team, Olaf!  I've been impressed by your
contributions to the project discussions and code to date, and look
forward to having you on the team.

I bumped the major version number up to 2 to reflect the new community
ownership of the project.  All the
[changes](http://ctemplate.googlecode.com/svn/tags/ctemplate-2.0/ChangeLog)
are related to the renaming.


### 18 January 2011 ###

The `google-ctemplate` Google Code page has been renamed to
`ctemplate`, in preparation for the project being renamed to
`ctemplate`.  In the coming weeks, I'll be stepping down as
maintainer for the ctemplate project, and as part of that Google is
relinquishing ownership of the project; it will now be entirely
community run.  The name change reflects that shift.


### 22 December 2011 ###

I've just released ctemplate 1.1.  The only functionality change is
obscure: when a reload is done (via, say, `ReloadAllIfChanged`), and a
new file has been created since the last reload that both a) has the
same filename as a template file that had been loaded in the past, and
b) is earlier on the template search-path than the previously loaded
file, **and** c) the previously loaded file hasn't changed since the
last reload, then at reload-time we now load the new file into the
template, replacing the old file.  Before we would only load the new
file if the old file had changed on disk, and would otherwise leave
the template alone.  Even more minor changes are in the
[ChangeLog](http://google-ctemplate.googlecode.com/svn/tags/ctemplate-1.1/ChangeLog).


### 26 August 2011 ###

I've just released ctemplate 1.0!  (I've decided 4 weeks is well
within the definition of "the next week or so"...)

A bit anti-climactic: there are no changes from ctemplate 1.0rc2.

### 29 July 2011 ###

I've just released ctemplate 1.0rc2.  This fixes a serious bug where I
had added #includes in installed header files, that tried to include
non-installed header files.  This means it was impossible to use
installed ctemplate; it only worked if you were using it from the
tarball directory.

I also fixed the unittest that was supposed to catch this, but didn't
(it was also building in the tarball directory).

If no further problems are found in the next week or so, I will
release ctemplate 1.0.

### 22 July 2011 ###

I've just released ctemplate 1.0rc1.  If no problems are found in the
next week or two, I will release ctemplate 1.0.

ctemplate 1.0rc1 has relatively few changes from ctemplate 0.99.
xml-escaping has been improved a bit.  A couple of bugs have been
fixed, including one where we were ignoring template-global sections
(a relatively new feature) in some places.  A full list of changes is
available in the
[ChangeLog](http://google-ctemplate.googlecode.com/svn/tags/ctemplate-1.0rc1/ChangeLog).

I've also changed the internal tools used to integrate
Google-supplied patches to ctemplate into the opensource release.
These new tools should result in more frequent updates with better
change descriptions.  They will also result in future ChangeLog
entries being much more verbose (for better or for worse).

### 24 January 2011 ###

I've just released ctemplate 0.99.  I expect this to be the last
release before ctemplate 1.0.  Code has settled down; the big change
here is some efficiency improvements to javascript template escaping.
There is also a bugfix for an obscure case where `ReloadAllIfChanged()`
is used with multiple ctemplate search paths, where files are deleted
in one part of the search path between reloads.  Unless you need
either of the above, there's no particular reason to upgrade.

A full list of changes is available in the
[ChangeLog](http://google-ctemplate.googlecode.com/svn/tags/ctemplate-0.99/ChangeLog).

### 23 September 2010 ###

I've just released ctemplate 0.98.  The changes are settling down as
we approach ctemplate 1.0 -- a few new default modifiers, a few
performance tweaks, a few portability improvements, but nothing
disruptive.

In my testing for this release, I noticed that the template regression
test (but not other template tests) would segfault on gcc 4.1.1 when
compiled with -O2.  This seems pretty clearly to be a compiler bug; if
you need to use gcc 4.1.1 to compile ctemplate, you may wish to build
via `./configure CXXFLAGS="-O1 -g"` just to be safe.

### 20 April 2010 ###

I've just released ctemplate 0.97.  This change consists primarily of a significant change to the API: the addition of the `TemplateCache` class, combined with deprecation of the `Template` class.

`TemplateCache` is a class that holds a collection of templates; this concept always existed in ctemplate, but was not previously exposed.  Many static methods of the `Template` class, such as `ReloadAllIfChanged()`, have become methods on `TemplateCache` instead (the `Template` methods have been retained for backwards compatibility.)  Other methods, such as `Expand()`, have become free functions.  In fact, the entire `Template` class has been deprecated.

The deprecation of `Template` calls for changes in all clients of the template code -- you can see in the example at the top of this page how the code has changed from `Template* tpl = ctemplate::Template::GetTemplate("example.tpl", ctemplate::DO_NOT_STRIP); tpl->Expand(&output, &dict);` to `ctemplate::ExpandTemplate("example.tpl", ctemplate::DO_NOT_STRIP, &dict, &output);`.  These changes will make the code simpler and more thread-safe.

Old code should continue to work -- the `Template` class remains -- but new code should use the new API, and old code should transition as convenient.  One old API method is intrinsically thread-unsafe, and should be prioritized to change: `tpl->ReloadIfChanged` should change to `ctemplate::Template::ReloadAllIfChanged()`.  Note this is a semantic change: all templates are now reloaded, rather than just one.  However, since templates are reloaded lazily, and only if they've changed on disk, I'm hopeful it will always be a reasonable change to make.

To go along with these changes, the documentation has been almost entirely revamped and made more accessible.  Obscure ctemplate features have been excised from the user's guide and moved into a separate reference document.  The new API is fully documented, including new flexibility around reloading templates, made available by the introduction of `TemplateCache`.

There are some more minor changes as well, such as the addition of #include guards in the auto-generated .tpl.h files, to make it safe to multiply-include them.  I've also been continuing the portability work: ctemplate should now work under Cygwin and MinGW.  A full list of changes is available in the [ChangeLog](http://google-ctemplate.googlecode.com/svn/tags/ctemplate-0.97/ChangeLog).

I know I've said this before, but I don't expect major API changes before the 1.0 release.  The most significant changes I expect to see are the potential removal of some of the 'forwarding' methods in the (deprecated) `Template` class.

### 12 June 2009 ###

I've just released ctemplate 0.95.  This is entirely an API cleanup release.  Actually, relatively little of the API proper has changed: `StringToTemplate` no longer takes an autoescape-context arg (instead you specify this as part of the template-string, using the `AUTOESCAPE` pragma).  A few obsolete constructs have gone away, such as the `TemplateFromString` class and `TemplateDictionary::html_escape` and friends (just use the top-level `html_escape`).  See the [ChangeLog](http://google-ctemplate.googlecode.com/svn/tags/ctemplate-0.95/ChangeLog) for a full list of these changes.

The biggest change is a renaming: the default namespace is now `ctemplate` rather than `google`, and the include directory is `ctemplate` rather than `google`.  Other namespaces, such as `template_modifiers`, have gone away.

All these changes will require you to modify your old code to get it working with ctemplate 0.95.  I've written a [script](http://google-ctemplate.googlecode.com/svn/trunk/contrib/convert_to_95.pl) to help you do that.  Please open an [issue](http://code.google.com/p/google-ctemplate/issues/list) if you see a problem with the script.  I've tested it, but not as widely as I'd like.  Also note the script will not be perfect for more complex constructs, which you will have to clean up by hand.

I hope (expect) the API is now stable, and we won't see any more such changes before ctemplate 1.0.  I tried to isolate them all in this release; except for the API changes, this release should behave identically to ctemplate 0.94.

### 7 May 2009 ###

I've just released ctemplate 0.94.  A few new features have been added, such as the ability to expand a template into your own custom `ExpandEmitter` instance, and the ability to hook the annotation system (typically used for debugging).  You can now
remove strings from the template cache in addition to adding them.  Also, there continues to be a trickle of new modifiers, in this case a modifier for URL's in a CSS context.

However, the most invasive changes were made for speed reasons.  The biggest is that (almost) all `TemplateDictionary` allocations are now done on the arena -- this includes allocations by the STL classes inside the dictionary.  This allows us to free all the memory at once, rather than item by item, and has yielded a 3-4% speed improvement in our tests.  I've combined this with a `small_map` class that stores items in a vector instead of a hash-map until we get to 3 or 4 items; this gives another speed increase in the (common) case a template has only a few sections or includes.

I also changed the hashing code to use [MurmurHash](http://murmurhash.googlepages.com/) everywhere, rather than the string hash function built into the STL library.  This should be faster.

All these changes should not be outwardly visible, but they do use more advanced features of C++ than ctemplate has to date.  This may result in some problems compiling, or conceivably when running.  If you see any, please file an [issue report](http://code.google.com/p/google-ctemplate/issues/list).

You can see a full list of changes on the [ChangeLog](http://google-ctemplate.googlecode.com/svn/tags/ctemplate-0.94/ChangeLog).

### 20 August 2008 ###

ctemplate 0.91 introduces the beginning of some API changes, as I look to clean up the API in preparation for ctemplate 1.0.  After 1.0, the API will remain backwards compatible, but until that time, the API may change.  Please take a look at the [ChangeLog](http://google-ctemplate.googlecode.com/svn/trunk/ChangeLog) to see if any of these changes affect you.

One change is the introduction of a new `PerExpandData` class, which holds some state that was formerly in the `TemplateDictionary` class.  I'm still not sure if this class is a good idea, if it should be separate from `TemplateDictionary` or a member, or what functionality should move there (for instance, should `SetTemplateGlobal` move there, since template-global variables are really, in some sense, per-expand variables?)  If you have any feedback, ideally based on your own experience using the current API, feel free to post it at `google-ctemplate@googlegroups.com`.

ctemplate also has several new features, including the addition of "separator" sections, and the ability to change the markup character (from `{{`).  See the [ChangeLog](http://google-ctemplate.googlecode.com/svn/trunk/ChangeLog) for a complete list, and the [howto documentation](http://google-ctemplate.googlecode.com/svn/trunk/doc/howto.html) for more details on these new features.
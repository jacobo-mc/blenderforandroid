# ##### BEGIN GPL LICENSE BLOCK #####
#
#  This program is free software; you can redistribute it and/or
#  modify it under the terms of the GNU General Public License
#  as published by the Free Software Foundation; either version 2
#  of the License, or (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software Foundation,
#  Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
# ##### END GPL LICENSE BLOCK #####

# <pep8 compliant>

# simple script to enable all addons, and disable

import bpy
import addon_utils

import sys
import imp


def reload_addons(do_reload=True, do_reverse=True):
    modules = addon_utils.modules({})
    modules.sort(key=lambda mod: mod.__name__)
    addons = bpy.context.user_preferences.addons

    # first disable all
    for mod_name in list(addons.keys()):
        addon_utils.disable(mod_name)

    assert(bool(addons) is False)

    # Run twice each time.
    for i in (0, 1):
        for mod in modules:
            mod_name = mod.__name__
            print("\tenabling:", mod_name)
            addon_utils.enable(mod_name)
            assert(mod_name in addons)

        for mod in addon_utils.modules({}):
            mod_name = mod.__name__
            print("\tdisabling:", mod_name)
            addon_utils.disable(mod_name)
            assert(not (mod_name in addons))

            # now test reloading
            if do_reload:
                imp.reload(sys.modules[mod_name])

            if do_reverse:
                # in case order matters when it shouldnt
                modules.reverse()


def main():
    reload_addons(do_reload=False, do_reverse=False)
    reload_addons(do_reload=False, do_reverse=True)
    reload_addons(do_reload=True, do_reverse=True)


if __name__ == "__main__":

    # So a python error exits(1)
    try:
        main()
    except:
        import traceback
        traceback.print_exc()
        sys.exit(1)

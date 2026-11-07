"""Kadan's in-game preview controller (P4GamePreviewScript.h), run in Lua with a stand-in game.

The controller remembers the last of the game's own worlds it saw loaded, and Kadan leaves a preview
area for that world. Before, Kadan always travelled to one fixed cave (Cave000_F00_P). Both rules give
that cave when it was the last world played; the controller's rule gives any other world the player
came from, and never a preview area.
"""
import re
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / 'Kadan/Source/Kadan/Private/P4GamePreviewScript.h'
OLD_NEUTRAL = 'Cave000_F00_P'
# Worlds of the game a player can be in before Play (tests may name maps).
STOCK_WORLDS = ('Cave000_F00_P', 'Area001_P', 'Area002_P', 'Area003_P', 'Area004_P', 'Area006_P', 'Area010_P',
                'Cave004_F01_P')


def controller_source():
    text = SCRIPT.read_text(encoding='utf-8')
    return ''.join(re.findall(r'R"KadanLua\((.*?)\)KadanLua"', text, re.S))


class Game:
    """The script API the controller's status reads: the loaded world, control and reservations."""

    def __init__(self, lua):
        self.world, self.control = '', True
        self.reserved = {}
        self.lua = lua
        self.step = None
        self.pauses = 0

    def install(self):
        lua = self.lua
        g = lua.globals()
        g.game = lua.table_from({
            'world': lambda: self.world,
            'has_control': lambda: self.control,
            'preview_scene': lambda handle: lua.table_from({'world_name': self.reserved[handle]}),
            'hold_pause': self.hold_pause,
            'release_pause': self.release_pause,
        })
        g.actor = lua.table_from({'alive': lambda a: False})
        g.player = lua.table_from({'captain': lambda index: 'captain'})
        g.reflection = lua.table_from({'find_class': lambda name: None})
        g.time = lua.table_from({'on_step': self.on_step})
        g.mod = lua.table_from({'log': lambda text: None})
        g.kadan_preview_scenes = lua.table_from({})

    def on_step(self, callback):
        self.step = callback
        return self.lua.table_from({})

    def hold_pause(self, what):
        self.pauses += 1
        return 'pause'

    def release_pause(self, handle):
        self.pauses -= 1
        return True

    def steps(self, count):
        for _ in range(count):
            self.step()

    def reserve(self, slot, world):
        handle = 'reservation-' + world
        self.reserved[handle] = world
        self.lua.globals().kadan_preview_scenes[slot] = handle


SAVE_FIELDS = ('Story', 'Quest', 'Prepo', 'FoW', 'Gen', 'Player')


class Saves:
    """The game instance's saves as Play and Stop reach them: reflection for the scene manager and the slots,
    and save.restore for putting a saved copy back."""

    def __init__(self, lua):
        self.lua = lua
        self.slots, self.restored, self.replaced = {}, [], []

    def install(self, restore=True):
        g = self.lua.globals()
        g.reflection = self.lua.table_from({'find_class': lambda name: 'GameplayStatics', 'call': self.call,
                                            'get': self.get, 'set': self.set})
        # A Lua function, as Tsuchi's calls are.
        wrap = self.lua.eval('function(f) return function(...) return f(...) end end')
        g.save = self.lua.table_from({'restore': wrap(self.restore)} if restore else {})

    def call(self, target, name, args):
        if name == 'GetGameInstance':
            return 'instance'
        if name == 'FindGameScene':
            return 'scene'
        if name == 'SaveGameToSlot':
            self.slots[args['SlotName']] = args['SaveGameObject']
            return True
        if name == 'LoadGameFromSlot':
            return 'copy:' + self.slots[args['SlotName']]
        if name in ('SceneReloadAll', 'DeleteGameInSlot'):
            return True
        return None

    def get(self, target, path):
        if path == 'GameSceneMgr':
            return 'manager'
        if path.startswith('CarrotSaveGames.'):
            return path.split('.', 1)[1]  # the current object of that kind
        return None

    def set(self, target, path, value):
        self.replaced.append(path)
        return True

    def restore(self, copy):
        self.restored.append(tuple(copy.split(':')))
        return True


class PreviewControllerTest(unittest.TestCase):
    def setUp(self):
        try:
            import lupa
        except ImportError:
            self.skipTest('lupa is not installed')
        self.lua = lupa.LuaRuntime(unpack_returned_tuples=True)
        self.game = Game(self.lua)
        self.game.install()
        self.controller = self.lua.execute(controller_source())

    def status(self):
        return self.controller.status('editor')

    def neutral(self, before):
        """Where Kadan leaves a loaded preview area for, after the player was in `before`."""
        self.game.world = before
        self.status()
        self.game.reserve(1, 'Area005_P')
        self.game.reserve(2, 'Area007_P')
        for world in ('Area005_P', 'Area007_P'):
            self.game.world = world
            self.status()
        return self.status().home

    def test_old_and_new_rule_agree_for_the_old_neutral_world(self):
        self.assertEqual(self.neutral(OLD_NEUTRAL), OLD_NEUTRAL)

    def test_every_stock_world_is_its_own_neutral_world(self):
        for world in STOCK_WORLDS:
            with self.subTest(world=world):
                self.setUp()
                self.assertEqual(self.neutral(world), world)

    def test_the_last_stock_world_wins_and_no_preview_area_does(self):
        self.game.world = 'Area001_P'
        self.status()
        self.game.world = OLD_NEUTRAL
        self.status()
        self.game.reserve(1, 'Area005_P')
        self.game.world = 'Area005_P'
        self.assertEqual(self.status().home, OLD_NEUTRAL)

    def test_a_world_without_control_is_not_remembered(self):
        self.game.world = OLD_NEUTRAL
        self.status()
        self.game.world, self.game.control = 'Title_P', False
        self.assertEqual(self.status().home, OLD_NEUTRAL)

    def test_a_long_preparation_keeps_the_stopped_preview(self):
        # K12: after Stop the controller holds the game's pause and the preview's starting state. A cook and upload
        # send nothing for longer than the lease, so the owner holds its lease while it prepares.
        who = 'a' * 32
        self.game.world = OLD_NEUTRAL
        self.assertEqual(self.controller.stop(who).state, 'stopped')
        self.assertEqual(self.game.pauses, 1)
        self.assertTrue(self.controller.prepare(who, 36000))
        self.game.steps(5000)
        self.assertEqual(self.status_of(who).state, 'stopped')
        self.assertTrue(self.status_of(who).owned)
        self.assertEqual(self.game.pauses, 1)
        # The hold ends with the preparation; a silent owner then loses the lease as before.
        self.assertTrue(self.controller.prepare(who, 0))
        self.game.steps(1801)
        self.assertEqual(self.controller.state, 'editing')
        self.assertEqual(self.game.pauses, 0)

    def test_a_preparation_hold_is_bounded(self):
        who = 'b' * 32
        self.controller.stop(who)
        self.assertFalse(self.controller.prepare('c' * 32, 36000))  # only the owner holds its lease
        self.controller.prepare(who, 10 ** 9)
        self.game.steps(35990)
        self.assertEqual(self.controller.state, 'stopped')
        self.game.steps(20)
        self.assertEqual(self.controller.state, 'editing')

    def status_of(self, who):
        return self.controller.status(who)

    def test_stop_puts_the_starting_state_back_into_the_games_own_saves(self):
        # The second-Play freeze (2026-10-28): Stop set new save objects in the game instance. The quest manager kept
        # the current area's records in the old story save, which the engine freed, and the game hung reading them.
        # The starting state now goes back through save.restore into the game's own objects, story before quests.
        saves = Saves(self.lua)
        saves.install()
        who = 'd' * 32
        self.game.world = OLD_NEUTRAL
        self.assertEqual(self.controller.play(who, 'Area001_P', 1, None).state, 'starting')
        self.assertEqual(saves.restored, [('copy', field) for field in SAVE_FIELDS])
        saves.restored.clear()
        self.controller.state = 'playing'  # the map finished loading
        self.assertEqual(self.controller.stop(who).state, 'stopped')
        self.assertEqual(saves.restored, [('copy', field) for field in SAVE_FIELDS])
        saves.restored.clear()
        self.controller.release(who)
        self.assertEqual(saves.restored, [('copy', field) for field in SAVE_FIELDS])
        self.assertEqual(saves.replaced, [])

    def test_play_without_restore_in_tsuchi_changes_nothing(self):
        saves = Saves(self.lua)
        saves.install(restore=False)
        with self.assertRaisesRegex(Exception, 'Update Tsuchi'):
            self.controller.play('e' * 32, 'Area001_P', 1, None)
        self.assertEqual(saves.slots, {})
        self.assertEqual(saves.replaced, [])

    def test_no_world_seen_gives_no_neutral_world(self):
        self.game.reserve(1, 'Area005_P')
        self.game.world = 'Area005_P'
        self.assertIsNone(self.status().home)


if __name__ == '__main__':
    unittest.main()

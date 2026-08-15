"""Session data structure -- id generation, is_live derivation, to_dict serialization."""

from arstream_server.session import Session


def test_new_generates_unique_ids():
    a = Session.new(peer_address="1.2.3.4:1000")
    b = Session.new(peer_address="1.2.3.4:1000")
    assert a.id != b.id


def test_to_dict_is_live_while_not_ended():
    s = Session.new(peer_address="1.2.3.4:1000")
    assert s.to_dict()["is_live"] is True
    s.ended_at = s.started_at + 1.0
    assert s.to_dict()["is_live"] is False


def test_to_dict_rounds_fps_to_one_decimal():
    s = Session.new(peer_address="1.2.3.4:1000")
    s.fps = 29.987654
    assert s.to_dict()["fps"] == 30.0

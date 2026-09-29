# Recompiles the carrier materials and prints compile errors (editor Python, headless).
import unreal
MEL = unreal.MaterialEditingLibrary
for n in ["M_CrewSuit", "M_CarrierHull", "M_CarrierDeck", "M_CarrierWake", "M_CarrierLight", "M_CarrierJbd", "M_HelmetShell"]:
    m = unreal.load_asset("/Game/Carrier/Materials/" + n)
    MEL.recompile_material(m)
    try:
        st = MEL.get_statistics(m)
        unreal.log("OSR mat %s vs=%s ps=%s" % (n, st.num_vertex_shader_instructions, st.num_pixel_shader_instructions))
    except Exception as e:
        unreal.log("OSR mat %s stats n/a %s" % (n, e))

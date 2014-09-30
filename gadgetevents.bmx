Const _EVENT_GADGET_ACTION:Int = 99



Type GadgetEvent
	Field source:Gadget
	
	Function Make:GadgetEvent( gadget:Gadget )
		Local e:GadgetEvent = New GadgetEvent
		e.source = gadget
		Return e
	EndFunction
EndType

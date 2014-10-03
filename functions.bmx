Global implemented:String[] = [ "go", "clear", "noise", "smooth", "expand", "contract", "darken", "lighten", "invert", "view" ]
Global implementedTemplates:TList = TList.FromArray( implemented )



Function ExecuteBox( box:Box, in:Box[] )
	Select box.kind
	Case "go"
		Clear()
		
		For Local root:Box = EachIn patch.boxes
			If root.ins = 0 And root.kind <> "go"
				root.Execute()
			EndIf
		Next
	Case "clear"
		Clear()
	Case "noise"
		Noise( box )
	Case "automata"
	Case "conway"
	Case "smooth"
		Smooth( box, in[0] )
	Case "expand"
		Expand( box, in[0] )
	Case "contract"
		Contract( box, in[0] )
	Case "darken"
		Darken( box, in[0], in[1] )
	Case "lighten"
		Lighten( box, in[0], in[1] )
	Case "invert"
		Invert( box, in[0] )
	Case "view"
		View( box, in[0] )
	EndSelect
End Function



Function Clear()
	patch.sparks.Clear()
		
	For Local _box:Box = EachIn patch.boxes
		_Clear( _box )
		_box.done = False
	Next
EndFunction



Function _Clear( box:Box )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		box.state[x, y] = 0
	Next
	Next
EndFunction



Function Noise( box:Box )
	Local density:Int = box.GetFloatProperty( "density" )
	
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		box.state[x, y] = ( Rand( 0, 100 ) < density )
	Next
	Next
EndFunction



Function Automata4( out:Box, in:Box, rules:Int[] )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		Local total:Int
		Local dxs:Int[] = [ 0, 1, 0, -1 ]
		Local dys:Int[] = [ -1, 0, 1, 0 ]
		
		For Local d:Int = 0 To 3
			Local dx:Int = dxs[d]
			Local dy:Int = dys[d]
			
			If ( Not ( dx = 0 And dy = 0 ) ) And ( x + dx < 20 ) And ( x + dx >= 0) And ( y + dy < 15 ) And ( y + dy >= 0 )
				total = total + in.state[ x + dx, y + dy ]
			EndIf
		Next
		
		out.state[ x, y ] = rules[ total ]
	Next
	Next
EndFunction



Function Automata5Sum4( out:Box, in:Box, rules:Int[] )
	'TODO
EndFunction



Function Automata5Sum5( out:Box, in:Box, rules:Int[] )
	'TODO
EndFunction



Function Automata8( out:Box, in:Box, rules:Int[] )
	'TODO
EndFunction



Function Automata9Sum8:Int( out:Box, in:Box, rules:Int[] )
	Local iterations:Int = out.GetIntProperty( "laps" )
	
	Local a:Int[ 20, 15 ]
	
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		a[ x, y ­] = in.state[ x, y ]
	Next
	Next
	
	While iterations >= 1
		For Local x:Int = 0 Until 20
		For Local y:Int = 0 Until 15
			Local total:Int
			
			For Local dx:Int = -1 To 1
			For Local dy:Int = -1 To 1
				If ( Not ( dx = 0 And dy = 0 ) ) And ( x + dx < 20 ) And ( x + dx >= 0) And ( y + dy < 15 ) And ( y + dy >= 0 )
					total = total + a[ x + dx, y + dy ]
				EndIf
			Next
			Next
			
			out.state[ x, y ] = rules[ total + a[ x, y ] * 9 ]
		Next
		Next
		
		iterations = iterations - 1
		
		If iterations >= 1
			For Local x:Int = 0 Until 20
			For Local y:Int = 0 Until 15
				a[ x, y ] = out.state[ x, y ]
			Next
			Next
		EndIf
	Wend
EndFunction



Function Automata9Sum9( out:Box, in:Box, rules:Int[] )
	'TODO
EndFunction



Function Expand( out:Box, in:Box )
	Automata4( out, in, [ 0, 1, 1, 1, 1 ] )
EndFunction



Function Contract( out:Box, in:Box )
	Automata4( out, in, [ 0, 0, 0, 0, 1 ] )
EndFunction



Function Smooth( out:Box, in:Box )
	Automata9Sum8( out, in, [ 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 1, 1, 1, 1, 1 ] )
EndFunction



Function Darken( out:Box, lo:Box, hi:Box )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		If hi.state[ x, y ] = 1
			out.state[ x, y ] = lo.state[ x, y ]
		Else
			out.state[ x, y ] = 0
		EndIf
	Next
	Next
EndFunction



Function Lighten( out:Box, lo:Box, hi:Box )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		If hi.state[ x, y ] = 0
			out.state[ x, y ] = lo.state[ x, y ]
		Else
			out.state[ x, y ] = 1
		EndIf
	Next
	Next
EndFunction



Function Invert( out:Box, in:Box )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		out.state[ x, y ] = 1 - in.state[ x, y ]
	Next
	Next
EndFunction



Function View( a:Box, b:Box )
	For Local x:Int = 0 Until 20
	For Local y:Int = 0 Until 15
		a.state[ x, y ] = b.state[ x, y ]
	Next
	Next
EndFunction